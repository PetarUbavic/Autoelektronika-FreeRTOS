// STANDARD INCLUDES
#include <stdio.h> 
#include <conio.h>

// KERNEL INCLUDES
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"

// HARDWARE SIMULATOR UTILITY FUNCTIONS  
#include "HW_access.h"

// DEFINES
#define MAX_CHARACTERS (10)
#define CR (13)

#define COM_CH_0 (0)
#define COM_CH_1 (1)
#define COM_CH_2 (2)


// TASK PRIORITIES 
#define	SERVICE_TASK_PRI		( tskIDLE_PRIORITY + 1 )
#define PROCESS_DATA_PRI		( tskIDLE_PRIORITY + (UBaseType_t)4 )
#define RECEIVE_COMMAND_PRI		( tskIDLE_PRIORITY + (UBaseType_t)5 )
#define RECEIVE_VALUE_PRI		( tskIDLE_PRIORITY + (UBaseType_t)7 )


// 7-SEG NUMBER DATABASE - ALL HEX DIGITS [ 0 1 2 3 4 5 6 7 8 9 A B C D E F ]
static const char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };

// TASKS: FORWARD DECLARATIONS 
static void LEDBar_Task(void* pvParameters);
static void ReceiveSens1ValueTask(void* pvParameters);
static void ReceiveSens2ValueTask(void* pvParameters);
static void ProcessDataTask(void* pvParameters);
static void AveragingDataTask(void* pvParameters);


// GLOBAL OS-HANDLES 
static SemaphoreHandle_t LED_INT_BinarySemaphore;
static SemaphoreHandle_t UART0_SEM, UART1_SEM, UART2_SEM;
static SemaphoreHandle_t RXC_BinarySemaphore1;
static QueueHandle_t Queue1, Queue2, QTemp1, QTemp2;
static TimerHandle_t per_TimerHandle;

// INTERRUPTS //
static uint32_t UARTInterrupt(void) {
	//interapt samo dize flagove, a prijem i obrada podataka se vrsi u taskovima
	BaseType_t xHigherPTW = pdFALSE;
	if (get_RXC_status(0) != 0) {
		if (xSemaphoreGiveFromISR(UART0_SEM, &xHigherPTW) != pdPASS) {
			printf("ERROR: UART_SEM0 GIVE\n");
		}
	}
	if (get_RXC_status(1) != 0) {
		if (xSemaphoreGiveFromISR(UART1_SEM, &xHigherPTW) != pdPASS) {
			printf("ERROR: UART_SEM1 GIVE\n");
		}
	}
	// za kanal 2 nemamo ovde nista jer sluzi samo za slanje
	portYIELD_FROM_ISR((uint32_t)xHigherPTW);
}

static uint32_t OnLED_ChangeInterrupt(void) {	// OPC - ON INPUT CHANGE - INTERRUPT HANDLER 
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(LED_INT_BinarySemaphore, &xHigherPTW);
	portYIELD_FROM_ISR(xHigherPTW);
}


// PERIODIC TIMER CALLBACK 
static void TimerCallback(TimerHandle_t tmH) {
	if (send_serial_character((uint8_t)COM_CH_0, (uint8_t)'T') != 0) { //ovo daje ritam aj da kazem tako
		printf("Error SEND_TRIGGER\n");
	}
	vTaskDelay(pdMS_TO_TICKS(100));
}

// MAIN - SYSTEM STARTUP POINT 
int main_program(void) {

	// INITIALIZATION //
	if (init_serial_uplink(COM_CH_0) != 0) {
		printf("ERROR: TX COM0 INIT\n");
	}
	if (init_serial_downlink(COM_CH_0) != 0) {
		printf("ERROR: RX COM0 INIT\n");
	}
	if (init_serial_uplink(COM_CH_1) != 0) {
		printf("ERROR: TX COM1 INIT\n");
	}
	if (init_serial_downlink(COM_CH_1) != 0) {
		printf("ERROR: RX COM1 INIT\n");
	}
	if (init_serial_uplink(COM_CH_2) != 0) {
		printf("ERROR: TX COM2 INIT\n");
	}
	if (init_serial_downlink(COM_CH_2) != 0) {
		printf("ERROR: RX COM2 INIT\n");
	}
	// SERIAL RECEPTION INTERRUPT HANDLER //
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, UARTInterrupt);
	
		// Setup UART peripherals
		UART0_SEM = xSemaphoreCreateBinary();
		if (UART0_SEM == NULL)
		{
			printf("ERROR: UART0_SEM CREATE");
		}

		UART1_SEM = xSemaphoreCreateBinary();
		if (UART1_SEM == NULL)
		{
			printf("ERROR: UART1_SEM CREATE");
		}

		// Create queue for UART data
		//uart_channel_0_queue = xQueueCreate(UART_CHANNEL_0_QUEUE_LENGTH, sizeof(uint8_t));


		Queue1 = xQueueCreate((uint8_t)10, (uint8_t)MAX_CHARACTERS * (uint8_t)sizeof(char));
		if (Queue1 == NULL) {
			printf("Error QUEUE1_CREATE\n");
		}

		Queue2 = xQueueCreate((uint8_t)10, (uint8_t)MAX_CHARACTERS * (uint8_t)sizeof(char));
		if (Queue2 == NULL) {
			printf("Error QUEUE2_CREATE\n");
		}

		QTemp1 = xQueueCreate((uint8_t)10, (uint8_t)MAX_CHARACTERS * (uint8_t)sizeof(char));
		if (QTemp1 == NULL) {
			printf("Error QTEMP1_CREATE\n");
		}

		QTemp2 = xQueueCreate((uint8_t)10, (uint8_t)MAX_CHARACTERS * (uint8_t)sizeof(char));
		if (QTemp2 == NULL) {
			printf("Error QTEMP2_CREATE\n");
		}
		
		// TIMERS //
		TimerHandle_t Timer200ms = xTimerCreate(
			NULL,
			pdMS_TO_TICKS(200),
			pdTRUE,
			NULL,
			TimerCallback);
		if (Timer200ms == NULL) {
			printf("Error TIMER_CREATE\n");
		}
		if (xTimerStart(Timer200ms, 0) != pdPASS) {
			printf("Error TIMER_START\n");
		}



		//TASKS
		BaseType_t status;
		status = xTaskCreate(
			ReceiveSens1ValueTask,
			"Prijem podataka sa senzora 1",
			configMINIMAL_STACK_SIZE,		
			NULL,
			(UBaseType_t)RECEIVE_VALUE_PRI,
			NULL);
		if (status != pdPASS) {
			printf("Error RECEIVE_VALUE_TASK_CREATE\n");
		}

		status = xTaskCreate(
			ReceiveSens2ValueTask,
			"Prijem podataka sa senzora 2",
			configMINIMAL_STACK_SIZE,		
			NULL,
			(UBaseType_t)RECEIVE_COMMAND_PRI,
			NULL);
		if (status != pdPASS) {
			printf("Error RECEIVE_COMMAND_TASK_CREATE\n");
		}

		status = xTaskCreate(
			AveragingDataTask,
			"Sredjivanje podataka sa senzora",
			configMINIMAL_STACK_SIZE,
			NULL,
			(UBaseType_t)RECEIVE_VALUE_PRI,
			NULL);
		if (status != pdPASS) {
			printf("Error AVERAGING_DATA_TASK_CREATE\n");
		}

		status = xTaskCreate(
			ProcessDataTask,
			"Obrada sredjenih podataka",
			configMINIMAL_STACK_SIZE,
			NULL,
			(UBaseType_t)PROCESS_DATA_PRI,
			NULL);
		if (status != pdPASS) {
			printf("Error RECEIVE_COMMAND_TASK_CREATE\n");
		}

		// Create task for UART communication
		//xTaskCreate(uart_channel_0_task, "UART_Channel_0_Task", UART_TASK_STACK_SIZE, NULL, UART_TASK_PRIORITY, NULL);

		// Start FreeRTOS scheduler
		vTaskStartScheduler();

		// Should never reach here
		return 0;
	}

// TASKS: IMPLEMENTATIONS
void LEDBar_Task(void* pvParameters) {
	unsigned i;
	uint8_t d;
	while (1) {
		xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
		get_LED_BAR(0, &d);
		i = 3;
		do {
			i--;
			select_7seg_digit(i);
			set_7seg_digit(hexnum[d % 10]);
			d /= 10;
		} while (i > 0);
	}
}

static void ReceiveSens1ValueTask(void* pvParameters) {
	static char tmpString[MAX_CHARACTERS];
	static uint8_t position = 0;
	static uint8_t value = 0;
	uint8_t cc = 0;
	for (;;) {
		printf("Kanal 0 primio karakter: %u\n", (unsigned)cc);
		if (xSemaphoreTake(UART0_SEM, portMAX_DELAY) != pdTRUE) {
			printf("ERROR: UART0_SEM TAKE\n");
		}
		if (get_serial_character(COM_CH_0, &cc) != 0) {
			printf("ERROR: GET CHARACTER 0\n");
		}
		if (cc == (uint8_t)CR) {
			value = (tmpString[0]-48) * 100;
			value += (tmpString[1] - 48) * 10;
			value += tmpString[2] - 48;
			tmpString[position] = '\0';
			position++;
			tmpString[position] = 'i';
			position = 0;
			if (xQueueSend(Queue1, &value, 0) != pdTRUE) {
				printf("Error QUEUE1_SEND\n");
			}
			printf("Hello: vrednost %u", value);
			value = 0;
		}
		else if (position < (uint8_t)MAX_CHARACTERS) {
			tmpString[position] = (char)cc;
			position++;
		}
		else {
			//MISRA
		}
	}
}

static void ReceiveSens2ValueTask(void* pvParameters) {
	static char tmpString[MAX_CHARACTERS];
	static uint8_t position = 0;
	static uint8_t value = 0;
	uint8_t cc = 0;
	for (;;) {
		printf("Kanal 2 primio karakter: %u\n", (unsigned)cc);
		if (xSemaphoreTake(UART1_SEM, portMAX_DELAY) != pdTRUE) {
			printf("ERROR: UART1_SEM TAKE\n");
		}
		if (get_serial_character(COM_CH_1, &cc) != 0) {
			printf("ERROR: GET CHARACTER 1\n");
		}
		if (cc == (uint8_t)CR) {

			if ((tmpString[0] >= '0' && tmpString[0] <= '9') &&
				(tmpString[1] >= '0' && tmpString[1] <= '9') &&
				(tmpString[2] >= '0' && tmpString[2] <= '9')) {

					value = (tmpString[0] - 48) * 100;
					if ((tmpString[0] - 48) >= 2) {
						// Overflow occurred
						// Handle overflow here, such as setting a flag or taking appropriate action
						printf("!!!! Overflow on channel 1 - Sensor2_OUT !!!!\n");
					}

					value += (tmpString[1] - 48) * 10;
					if (value > 245) {
						// Overflow occurred
						// Handle overflow here, such as setting a flag or taking appropriate action
						printf("!!!! Overflow on channel 1 - Sensor2_OUT !!!!\n");
					}
					value += tmpString[2] - 48;

					if (value > 150) {
						// Invalid input
						printf("Invalid input - Sensor2\n");
					}

					tmpString[position] = '\0';
					position++;
					tmpString[position] = 'i';
					position = 0;
					if (xQueueSend(Queue2, &value, 0) != pdTRUE) {
						printf("Error QUEUE2_SEND\n");
					}
					printf("Hello1: vrednost %u", value);
					value = 0;
			}			
		}

		else if (position < (uint8_t)MAX_CHARACTERS) {
			tmpString[position] = (char)cc;
			position++;
		}
		else {
			//MISRA
		}
	}
}


static void AveragingDataTask(void* pvParameters) {

	uint16_t tmp1Val = 0;
	uint16_t tmp2Val = 0;
	static uint16_t tmp1 = 0;
	static uint16_t tmp2 = 0;
	static uint16_t temp1 = 0;
	static uint16_t temp2 = 0;
	static uint8_t cnt1 = 0;
	static uint8_t cnt2 = 0;

	for (;;)
	{
		if (xQueueReceive(Queue1, &tmp1Val, portMAX_DELAY) != pdTRUE) {
			printf("Error QUEUE1_RECEIVE\n");
		}
		else {
			tmp1 += (uint16_t)tmp1Val;
			cnt1++;
			printf("\nOVDE SAAMM\n");
		}

		if (cnt1 == 5) {
			temp1 = (uint16_t)tmp1 / (uint16_t)5;
			temp1 /= 1.5;													// koeficijent iz zadatka prakticno
			if (xQueueSend(QTemp1, &temp1, 0) != pdTRUE) {
				printf("Error QTEMP1_SEND\n");
			}
			cnt1 = 0;
			tmp1 = 0;
		}

		if (xQueueReceive(Queue2, &tmp2Val, portMAX_DELAY) != pdTRUE) {
			printf("Error QUEUE2_RECEIVE\n");
		}
		else {
			tmp2 += (uint16_t)tmp2Val;
			cnt2++;
			printf("\nOVDE SAAMM\n");
		}

		if (cnt2 == 5) {
			temp2 = (uint16_t)tmp2 / (uint16_t)5;
			temp2 /= 1.5;													// koeficijent iz zadatka prakticno
			if (xQueueSend(QTemp2, &temp2, 0) != pdTRUE) {
				printf("Error QTEMP2_SEND\n");
			}
			cnt2 = 0;
			tmp2 = 0;
		}

		printf("\n CNT1 je: %u\n", cnt1);
		printf("\nRED: %u\n", temp1);

	}
}


static void ProcessDataTask(void* pvParameters) {

	static uint16_t STemp1 = 0;
	static uint16_t STemp2 = 0;
	static uint16_t motorTemp = 0;
	static uint8_t ventilatorFlag = 0;

	for (;;) {

		if (xQueueReceive(QTemp1, &STemp1, portMAX_DELAY) != pdTRUE) {
			printf("Error QTemp1_RECEIVE\n");
		}

		if (xQueueReceive(QTemp2, &STemp2, portMAX_DELAY) != pdTRUE) {
			printf("Error QTemp2_RECEIVE\n");
		}

		if (STemp1 - STemp2 > 5 || STemp2 - STemp1 > 5) {					// razlika izmedju ocitavanja dva senzora veca od 5 stepeni celzijusa
			//ukljuci alarm za pogresno ocitavanje senzora, jedan stubac LED bara da blinka periodom od 1000ms
		}

		motorTemp = (STemp1 + STemp2) / 2;

		if (motorTemp > 90) {
			ventilatorFlag = 1;
			//upaliti ventilator - donja LED dioda drugog stupca LED bara
			//poslati poruku da je ventilator ukljucen
		}

		else if (motorTemp < 85) {

			if (ventilatorFlag == 1) {
				//poslati poruku da je ventilator iskljucen
			}
			ventilatorFlag = 0;
			//ugasiti ventilator	
		}

		else {
			//MISRA
		}

		if (motorTemp > 95) {
			//poruka upozorenja i ukljuciti treci stubac LED bara da blinka periodom od 100ms
		}

		//na LCD displeju prikazati trenutnu vrednost temperature motora, brzina osvezavanja podatak 100ms
	}

}

static void LedBarTask(void* pvParameters) {

}

static void DisplayTask(void* pvParameters) {

}