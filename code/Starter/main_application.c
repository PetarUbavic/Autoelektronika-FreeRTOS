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
#define AVERAGE_DATA_PRI		( tskIDLE_PRIORITY + (UBaseType_t)4 )
#define PROCESS_DATA_PRI		( tskIDLE_PRIORITY + (UBaseType_t)5 )
#define RECEIVE_VALUE_PRI1		( tskIDLE_PRIORITY + (UBaseType_t)6 )
#define RECEIVE_VALUE_PRI2		( tskIDLE_PRIORITY + (UBaseType_t)7 )

/*
typedef struct LEDBar {
	uint8_t barsNum;
	uint8_t diodesNum;
}LEDBar;
*/

// TASKS: FORWARD DECLARATIONS 
static void LEDBar_Task(void* pvParameters);
static void ReceiveSens1ValueTask(void* pvParameters);
static void ReceiveSens2ValueTask(void* pvParameters);
static void ProcessDataTask(void* pvParameters);
static void AveragingDataTask(void* pvParameters);
static void LedBarTask(void* pvParameters);
static void DisplayTask(void* pvParameters);
static void SendMessageTask(void* pvParameters);


// GLOBAL OS-HANDLES 
static SemaphoreHandle_t LED_INT_BinarySemaphore;
static SemaphoreHandle_t UART0_SEM, UART1_SEM, UART2_SEM;
static SemaphoreHandle_t RXC_BinarySemaphore1;
static SemaphoreHandle_t WrongSensing_SEM;
static SemaphoreHandle_t Sens1_SEM, Sens2_SEM;
static SemaphoreHandle_t Display_SEM;
static SemaphoreHandle_t Message_SEM;
static QueueHandle_t Queue1, Queue2, QTemp1, QTemp2, QMotorTemp;
static TimerHandle_t per_TimerHandle;

// INTERRUPTS //
static uint32_t UARTInterrupt(void) {
	//interapt samo dize flagove, a prijem i obrada podataka se vrsi u taskovima;
	BaseType_t xHigherPTW = pdFALSE;
	if (get_RXC_status(COM_CH_0) != NULL) {
		if (xSemaphoreGiveFromISR(UART0_SEM, &xHigherPTW) != pdPASS) {
			printf("ERROR: UART_SEM0 GIVE\n");
		}
		else {
			//printf("UART0 GIVEN\n");
		}
	}
	if (get_RXC_status(COM_CH_1) != NULL) {
		if (xSemaphoreGiveFromISR(UART1_SEM, &xHigherPTW) != pdPASS) {
			printf("ERROR: UART_SEM1 GIVE\n");
		}
		else {
			//printf("\n---------------------------UART1 GIVEN---------------------------\n");
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

static uint32_t MessageInterrupt(void) {

	BaseType_t xHigherPTW = pdFALSE;

	if (get_TBE_status(2) != NULL) {
		if (xSemaphoreGiveFromISR(Message_SEM, &xHigherPTW) != pdPASS) {
			printf("ERROR: Message_SEM Give\n");
		}
	}

	portYIELD_FROM_ISR((uint32_t)xHigherPTW);
}


// PERIODIC TIMER CALLBACK 
static void TimerCallback200(TimerHandle_t tmH) {
	if (send_serial_character((uint8_t)COM_CH_0, (uint8_t)'T') != 0) { //ovo daje ritam aj da kazem tako
		printf("Error SEND_TRIGGER\n");
	}

	vTaskDelay(pdMS_TO_TICKS(400));

	if (send_serial_character((uint8_t)COM_CH_1, (uint8_t)'T') != 0) { //ovo daje ritam aj da kazem tako
		printf("Error SEND_TRIGGER\n");
	}

	//vTaskDelay(pdMS_TO_TICKS(50));
}

static void TimerCallback100(TimerHandle_t tmH) {
	if (xSemaphoreGive(Display_SEM) != pdTRUE) {
		printf("Error: Semaphore Display_SEM Give\n");
	}
	//vTaskDelay(pdMS_TO_TICKS(100));
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

	if (init_LED_comm() != 0) {
		printf("ERROR: LED BAR INIT\n");
	}

	if (init_7seg_comm() != 0) {
		printf("ERROR: 7SEG DISPLAY INIT\n");
	}

	// SERIAL RECEPTION INTERRUPT HANDLER //
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, UARTInterrupt);
	vPortSetInterruptHandler(portINTERRUPT_SRL_TBE, MessageInterrupt);

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

	Sens1_SEM = xSemaphoreCreateBinary();
	if (Sens1_SEM == NULL) {
		printf("ERROR: Sens1_SEM CREATE");
	}

	Sens2_SEM = xSemaphoreCreateBinary();
	if (Sens2_SEM == NULL) {
		printf("ERROR: Sens1_SEM CREATE");
	}

	WrongSensing_SEM = xSemaphoreCreateBinary();
	if (WrongSensing_SEM == NULL) {
		printf("ERROR: WRONGSENSING_SEM CREATE");
	}

	Display_SEM = xSemaphoreCreateBinary();
	if (Display_SEM == NULL) {
		printf("Error: Display_SEM Create\n");
	}

	Message_SEM = xSemaphoreCreateBinary();
	if (Message_SEM == NULL) {
		printf("ERROR: Message_SEM Create\n");
	}

	// Create queue for UART data
	//uart_channel_0_queue = xQueueCreate(UART_CHANNEL_0_QUEUE_LENGTH, sizeof(uint8_t));


	Queue1 = xQueueCreate((uint8_t)15, (uint8_t)MAX_CHARACTERS * (uint8_t)sizeof(uint8_t));		//15 mesta zato sto mora da stane 5, pa kao jos 3 puta toliko da ima lufta ajd
	if (Queue1 == NULL) {
		printf("Error QUEUE1_CREATE\n");
	}

	Queue2 = xQueueCreate((uint8_t)15, (uint8_t)MAX_CHARACTERS * (uint8_t)sizeof(char));		//15 mesta zato sto mora da stane 5, pa kao jos 3 puta toliko da ima lufta ajd
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

	QMotorTemp = xQueueCreate((uint8_t)10, (uint8_t)MAX_CHARACTERS * (uint8_t)sizeof(char));
	if (QMotorTemp == NULL) {
		printf("Error QTEMP2_CREATE\n");
	}

	// TIMERS //
	TimerHandle_t Timer200ms = xTimerCreate(
		"Timer for UART trigger",
		pdMS_TO_TICKS(550), // bilo 1000
		pdTRUE,
		NULL,
		TimerCallback200);
	if (Timer200ms == NULL) {
		printf("Error Timer200ms Create\n");
	}
	if (xTimerStart(Timer200ms, 0) != pdPASS) {
		printf("Error Timer200ms Start\n");
	}

	/*TimerHandle_t Timer100ms = xTimerCreate(
		"Timer for Display",
		pdMS_TO_TICKS(100),
		pdTRUE,
		NULL,
		TimerCallback100);
	if (Timer100ms == NULL) {
		printf("Error Timer100ms Create\n");
	}
	if (xTimerStart(Timer100ms, 0) != pdPASS) {
		printf("Error Timer100ms Start\n");
	}
	*/


	//TASKS
	BaseType_t status;
	status = xTaskCreate(
		ReceiveSens1ValueTask,
		"Prijem podataka sa senzora 1",
		configMINIMAL_STACK_SIZE,
		NULL,
		(UBaseType_t)RECEIVE_VALUE_PRI2,
		NULL);
	if (status != pdPASS) {
		printf("Error RECEIVE_VALUE_TASK_CREATE\n");
	}

	status = xTaskCreate(
		ReceiveSens2ValueTask,
		"Prijem podataka sa senzora 2",
		configMINIMAL_STACK_SIZE,
		NULL,
		(UBaseType_t)RECEIVE_VALUE_PRI2,
		NULL);
	if (status != pdPASS) {
		printf("Error RECEIVE_COMMAND_TASK_CREATE\n");
	}

	status = xTaskCreate(
		AveragingDataTask,
		"Sredjivanje podataka sa senzora",
		configMINIMAL_STACK_SIZE,
		NULL,
		(UBaseType_t)AVERAGE_DATA_PRI,
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

	status = xTaskCreate(
		DisplayTask,
		"Prikaz temperature na displej",
		configMINIMAL_STACK_SIZE,
		NULL,
		(UBaseType_t)PROCESS_DATA_PRI,
		NULL);
	if (status != pdPASS) {
		printf("Error RECEIVE_COMMAND_TASK_CREATE\n");
	}
	
	/*status = xTaskCreate(
		LedBarTask,
		"LED bar alarmi",
		configMINIMAL_STACK_SIZE,
		NULL,
		(UBaseType_t)PROCESS_DATA_PRI,
		NULL);
	if (status != pdPASS) {
		printf("Error RECEIVE_COMMAND_TASK_CREATE\n");
	}
	*/

	status = xTaskCreate(
		SendMessageTask,
		"Task za slanje poruka na serijsku",
		configMINIMAL_STACK_SIZE,
		NULL,
		(UBaseType_t)PROCESS_DATA_PRI,
		NULL);
	if (status != pdPASS) {
		printf("ERROR: SendMessageTask Create\n");
	}

	// Create task for UART communication
	//xTaskCreate(uart_channel_0_task, "UART_Channel_0_Task", UART_TASK_STACK_SIZE, NULL, UART_TASK_PRIORITY, NULL);


	//vTaskDelay(pdMS_TO_TICKS(3000));

	// Start FreeRTOS scheduler
	vTaskStartScheduler();

	// Should never reach here
	return 0;
}

// TASKS: IMPLEMENTATIONS

static void ReceiveSens1ValueTask(void* pvParameters) {
	static char tmpString[MAX_CHARACTERS];
	static uint8_t position = 0;
	static uint8_t* value = 0;
	uint8_t cc = 0;
	for (;;) {
		//printf("Kanal 1 primio karakter: %u\n", (unsigned)cc);
		if (xSemaphoreTake(UART0_SEM, portMAX_DELAY) != pdTRUE) {
			printf("ERROR: UART0_SEM TAKE\n");
		}
		if (get_serial_character(COM_CH_0, &cc) != 0) {
			printf("ERROR: GET CHARACTER 0\n");
		}
		if (cc == (uint8_t)CR) {

			if ((tmpString[0] >= '0' && tmpString[0] <= '9') &&
				(tmpString[1] >= '0' && tmpString[1] <= '9') &&
				(tmpString[2] >= '0' && tmpString[2] <= '9')) {						// Provera da li su svi uneti karakteri zaista brojevi

				if ((uint8_t)(tmpString[0] - 48) > (uint8_t)1) {				// Provera da li je prvi karakter veci od 1 ako jeste, ne valja unos, ako nije, znaci sve ok, ide u else i racuna broj
					// Overflow occurred
					// Handle overflow here, such as setting a flag or taking appropriate action
					printf("!!!! Posibility of Overflow on Channel 0 - Sensor1 !!!!\n");
					printf("Invalid input - Sensor1\n");
					value = 0;
				}

				else if (((uint8_t)((tmpString[0] - 48) * 10) + (uint8_t)(tmpString[1] - 48)) > (uint8_t)15) { // U sustini proverava da li ce broj biti veci od 150, ali proverava da li su prvi_karakter*10 + drugi_karakter > 15 kako bi izbegao eventualni overflow
					printf("Invalid input - Sensor1\n");
					value = 0;
				}

				else {
					value = (tmpString[0] - 48) * 100;
					value += (uint8_t)(tmpString[1] - 48) * (uint8_t)10;
					value += (uint8_t)tmpString[2] - (uint8_t)48;
				}

				if (xQueueSend(Queue1, &value, 0) != pdTRUE) {
					printf("Error QUEUE1_SEND\n");
				}

				else {
					if (xSemaphoreGive(Sens1_SEM) != pdTRUE) {
						//printf("Error: Sens1_SEM Give\n");
					}
				}

				position = 0;

				//printf("-----Broj mesta u Q1: %u", uxQueueSpacesAvailable(Queue1));
				//printf("----------Poslata Vrednost 1: %u\n", value);

				if (uxQueueSpacesAvailable(Queue1) == 1) {
					xQueueReset(Queue1);
				}

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

static void ReceiveSens2ValueTask(void* pvParameters) {
	static char tmpString[MAX_CHARACTERS];
	static uint8_t position = 0;
	static uint8_t* value = 0;
	uint8_t cc = 0;
	for (;;) {
		//printf("Kanal 2 primio karakter: %u\n", (unsigned)cc);
		if (xSemaphoreTake(UART1_SEM, portMAX_DELAY) != pdTRUE) {
			printf("ERROR: UART1_SEM TAKE\n");
		}
		if (get_serial_character(COM_CH_1, &cc) != 0) {
			printf("ERROR: GET CHARACTER 1\n");
		}
		if (cc == (uint8_t)CR) {

			if ((tmpString[0] >= '0' && tmpString[0] <= '9') &&
				(tmpString[1] >= '0' && tmpString[1] <= '9') &&
				(tmpString[2] >= '0' && tmpString[2] <= '9')) {						// Provera da li su svi uneti karakteri zaista brojevi

				if ((uint8_t)(tmpString[0] - 48) > (uint8_t)1) {				// Provera da li je prvi karakter veci od 1 ako jeste, ne valja unos, ako nije, znaci sve ok, ide u else i racuna broj
					// Overflow occurred
					// Handle overflow here, such as setting a flag or taking appropriate action
					printf("!!!! Posibility of Overflow on Channel 1 - Sensor2 !!!!\n");
					printf("Invalid input - Sensor2\n");
					value = 0;
				}

				else if (((uint8_t)((tmpString[0] - 48) * 10) + (uint8_t)(tmpString[1] - 48)) > (uint8_t)15) { // U sustini proverava da li ce broj biti veci od 150, ali proverava da li su prvi_karakter*10 + drugi_karakter > 15 kako bi izbegao eventualni overflow
					printf("Invalid input - Sensor2\n");
					value = 0;
				}

				else {
					value = (tmpString[0] - 48) * 100;
					value += (uint8_t)(tmpString[1] - 48) * (uint8_t)10;
					value += (uint8_t)tmpString[2] - (uint8_t)48;
				}

				if (xQueueSend(Queue2, &value, 0) != pdTRUE) {
					printf("Error QUEUE2_SEND\n");
				}

				else {
					if (xSemaphoreGive(Sens2_SEM) != pdTRUE) {
						//printf("Error: Sens2_SEM Give\n");
					}
				}

				position = 0;
				//printf("-----Broj mesta u Q2: %u", uxQueueSpacesAvailable(Queue2));
				//printf("---------------------Vrednost 2: %u\n", value);

				if (uxQueueSpacesAvailable(Queue2) == 1) {
					xQueueReset(Queue2);
				}

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
	uint16_t tmp1 = 0;
	uint16_t tmp2 = 0;
	static double temp1 = 0; //mozda ne treba static
	static double temp2 = 0; //mozda ne treba static
	static uint8_t cnt1 = 0;
	static uint8_t cnt2 = 0;

	for (;;)
	{

		if (xSemaphoreTake(Sens1_SEM, portMAX_DELAY/*pdMS_TO_TICKS(3000) */ ) != pdTRUE) {
			printf("Error: Sens1_SEM Receive\n");
		}
		else {
			//printf("\-----U AVERAGE SAM------\n");
			if (xQueueReceive(Queue1, &tmp1Val, portMAX_DELAY/*pdMS_TO_TICKS(300)*/) != pdTRUE) {
				printf("Error: QUEUE1_RECEIVE\n");
			}
			else {
				//printf("Primim 1: %d--------", tmp1Val);
				tmp1 += (uint16_t)tmp1Val;
				cnt1++;
				//printf("\n--------------------OVDE SAAMM %u\n", tmp1);
			}
			
			//printf("-----DOBIO SAM %u: \n", tmp1Val);

			if (cnt1 == 5) {
				temp1 = (double)tmp1 / (double)5;
				temp1 /= (double)1.5;													// koeficijent iz zadatka prakticno
				if (xQueueSend(QTemp1, &temp1, 0) != pdTRUE) {
					printf("Error QTEMP1_SEND\n");
				}
				else {
					//printf("POSALJEM 1: %f\n", temp1);
				}
				cnt1 = 0;
				tmp1 = 0;
				//printf("\n-------TMP1 %u\n", tmp1);
			}
		}

		
		if (xSemaphoreTake(Sens2_SEM, portMAX_DELAY/*pdMS_TO_TICKS(3000)*/) != pdTRUE) {
			printf("Error: Sens2_SEM Receive\n");
		}

		else {
			if (xQueueReceive(Queue2, &tmp2Val, portMAX_DELAY/*pdMS_TO_TICKS(300)*/) != pdTRUE) {
				printf("Error QUEUE2_RECEIVE\n");
			}

			else {
				tmp2 += (uint16_t)tmp2Val;
				cnt2++;
				//printf("\n--------TMP2 %u\n", tmp2);
			}

			//printf("-----DOBIO SAM %u: \n", tmp2Val);


			if (cnt2 == 5) {
				temp2 = (double)tmp2 / (double)5;
				temp2 /= (double)1.5;													// koeficijent iz zadatka prakticno
				if (xQueueSend(QTemp2, &temp2, 0) != pdTRUE) {
					printf("Error QTEMP2_SEND\n");
				}
				else {
					//printf("POSALJEM 2: %f\n", temp2);
				}
				cnt2 = 0;
				tmp2 = 0;
				//printf("\n-------------OVDE SAAMM %u\n", tmp2);
			}
	
		}

		//printf("\n CNT1 je: %u ---- %u\n", cnt1, cnt2);
		//printf("\nRED: %f ,,,, %f\n", temp1, temp2);
		//vTaskDelay(pdMS_TO_TICKS(500));
	}
}

static void ProcessDataTask(void* pvParameters) {

	double STemp1 = 0;
	double STemp2 = 0;
	static double motorTemp = 0;
	static uint8_t ventilatorFlag = 0;

	for (;;) {

		if (xQueueReceive(QTemp1, &STemp1, pdMS_TO_TICKS(3000)) != pdTRUE) {
			printf("Error QTemp1_RECEIVE\n");
		}

		if (xQueueReceive(QTemp2, &STemp2, pdMS_TO_TICKS(3000)) != pdTRUE) {
			printf("Error QTemp2_RECEIVE\n");
		}

		printf("STEMP 1 i 2 %f ,,,, %f\n", STemp1, STemp2);
		printf("-----RAZLIKA JE: %f\n", STemp1 - STemp2);
		if (STemp1 - STemp2 > 5 || STemp2 - STemp1 > 5) {					// razlika izmedju ocitavanja dva senzora veca od 5 stepeni celzijusa
			//ukljuci alarm za pogresno ocitavanje senzora, jedan stubac LED bara da blinka periodom od 1000ms
			/*if (xSemaphoreGive(WrongSensing_SEM) != pdTRUE) {
				printf("Error WrongSensing_SEM Give\n");
			}*/

			printf("--------RAZLIKA U TEMPERATURI PREVELIKA---------\n");
		}

		motorTemp = (STemp1 + STemp2) / (double)2;

		printf("Temperatura Motora je: %f\n", motorTemp);
		if (xQueueSend(QMotorTemp, &motorTemp, 0) != pdTRUE) {
			printf("Error: Queue Send QMotorTemp\n");
		}

		if (motorTemp > 90) {
			ventilatorFlag = 1;
			//upaliti ventilator - donja LED dioda drugog stupca LED bara
			//poslati poruku da je ventilator ukljucen
			printf("-----VRUCINA------");
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

	static uint8_t i = 0;
	uint8_t d;

	for (;;) {
		/*if (xSemaphoreTake(Display_SEM, pdMS_TO_TICKS(200)) != pdTRUE) {
			printf("Error: Semaphore Display_SEM Take\n");
		}*/
		//xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
		printf("Led Bar SAM---------");
		
		set_LED_BAR(0, 0xff);

		set_LED_BAR(2, 0x00);
		set_LED_BAR(2, 0x01);
		set_LED_BAR(2, 0x03);
		set_LED_BAR(2, 0x07);
		set_LED_BAR(2, 0x0f);
		set_LED_BAR(2, 0x1f);
		set_LED_BAR(2, 0x3f);
		set_LED_BAR(2, 0x7f);
		set_LED_BAR(2, 0xff);

		vTaskDelay(pdMS_TO_TICKS(5000));
		/*i = 3;
		do {
			i--;
			select_7seg_digit(i);
			set_7seg_digit(hexnum[d % 10]);
			d /= 10;
		} while (i > 0);*/
	}
}

static void DisplayTask(void* pvParameters) {

	// 7-SEG NUMBER DATABASE - ALL HEX DIGITS [ 0 1 2 3 4 5 6 7 8 9 A B C D E F ]
	/*static const char hexnum[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71};*/

	const uint8_t character[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
	static double motorTemp = 75;

	for (;;) {
		if (xSemaphoreTake(Display_SEM, pdMS_TO_TICKS(10000)) != pdTRUE) {
			printf("Error: Semaphore Display_SEM Take\n");
		}

		if (xQueuePeek(QMotorTemp, &motorTemp, 0) != pdTRUE) {	//peek zato sto treba da iscitava 10 puta brze nego sto se upisuje
			printf("Error: Queue QMotorTemp Receive\n");
		}
		
		printf("------------PRIMIO TEMPERATURU %f---------\n", motorTemp);

		if ((uint8_t)select_7seg_digit((uint8_t)0) != (uint8_t)0) {		// odabiremo skroz levu cifru
			printf("Error: Select Display 3\n");
		}
		if ((uint8_t)set_7seg_digit(character[(uint8_t)motorTemp / (uint8_t)100]) != (uint8_t)0) {
			printf("Error: Set Display 3\n");
		}
		if ((uint8_t)select_7seg_digit((uint8_t)1) != (uint8_t)0) {
			printf("Error: Select Display 2\n");
		}
		if ((uint8_t)set_7seg_digit(character[((uint8_t)motorTemp / (uint8_t)10) % (uint8_t)10]) != (uint8_t)0) { //ide jos % 10 zato sto ako je motorTemp 100 onda je 100 /10 = 10 a ne moze da prikaze to, a treba 0 da prikaze, pa onda ide jos % 10 da bi dobio 0
			printf("Error: Set Display 2\n");
		}
		if ((uint8_t)select_7seg_digit((uint8_t)2) != (uint8_t)0) {
			printf("Error: Select Display 1\n");
		}
		if ((uint8_t)set_7seg_digit(character[(uint8_t)motorTemp % (uint8_t)10]) != (uint8_t)0) {
			printf("Error: Set Display 1\n");
		}

		vTaskDelay(pdMS_TO_TICKS(1000));

	}
}

static void SendMessageTask(void* pvParameters) {
	
	static const char message1[] = "Ventilator ukljucen\13";
	static const char message2[] = "Ventilator iskljucen\13";
	static const char message3[] = "Temperatura previsoka\13";
	static const char message4[] = "Senzor 1 neispravan\13";
	static const char message5[] = "Senzor 2 neispravan\13";

	static uint8_t n = 0;

	for (;;) {

		for (n = 0; n < sizeof(message1); n++) {
			send_serial_character(2, message1[n]);
			vTaskDelay(pdMS_TO_TICKS(70));
		}

		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}