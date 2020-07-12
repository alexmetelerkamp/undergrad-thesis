
// Atmega1280 Asset Monitoring
// 
// Written by Alex Metelerkamp March 2011
// 
// Transmit methodology learnt from Sage Radachowskys "SendWithInterrupt.c" - Sage Radachowsky, Feb-Mar 2008 ("sager" at "mit" dot "edu") 
// Utilising Dean Camera's "LightWeightRingBuff.h" - 2010 dean [at] fourwalledcubicle [dot] com
// 

//*******************************************************************************
//******************************************************************************* 
//								DEFINITIONS 
//*******************************************************************************
//******************************************************************************* 

// CPU frequency (external osc at 11MHz)
#define F_CPU 11059200UL   // 11.0592 MHz external crystal

// UART baud rate in bps
#define UART1_BAUDRATE  115200 
#define UART2_BAUDRATE  115200 
#define UART3_BAUDRATE  9600


//calculate prescale
#define UART1_BAUD_PRESCALE (((F_CPU / (UART2_BAUDRATE * 16UL))) - 1) 
#define UART2_BAUD_PRESCALE (((F_CPU / (UART2_BAUDRATE * 16UL))) - 1) 
#define UART3_BAUD_PRESCALE (((F_CPU / (UART3_BAUDRATE * 16UL))) - 1) 


//tolerance on successive speed value acceptance
#define SPEED_TOLERANCE 10

//interval to send SMS update of odometer
#define SMS_INTERVAL	60	//seconds

//specifiy the EEPROM intervals
#define EEPROM_MINPERIOD 120	//minimum time between EEPROM writes
#define EEPROM_MAXPERIOD 3600	//maximum time between EEPROM write

//*******************************************************************************
//******************************************************************************* 
//							HEADER FILES 
//*******************************************************************************
//******************************************************************************* 

#include <ctype.h> 
#include <string.h> 
#include <stdint.h> 
#include <stdio.h> 
#include <stdlib.h>
#include <avr/io.h> 
#include <avr/interrupt.h> 
#include <avr/pgmspace.h> 
#include <util/delay.h>
#include <avr/eeprom.h>
#include "LightWeightRingBuff.h"

//*******************************************************************************
//******************************************************************************* 
//							GLOBAL VARIABLES 
//*******************************************************************************
//******************************************************************************* 


volatile int	Uart1OutSendingInProgress=0; // 1 = sending is in progress 
volatile int	Uart1InReceivingInProgress=0; // 1 = Receiving is in progress 

volatile int	Uart2OutSendingInProgress=0; // 1 = sending is in progress 
volatile int	Uart2InReceivingInProgress=0; // 1 = Receiving is in progress 

volatile int	Uart3OutSendingInProgress=0; // 1 = sending is in progress 
volatile int	Uart3InReceivingInProgress=0; // 1 = Receiving is in progress 

int uart2_count=0;


//Input and output buffers
RingBuff_t	InBuffer_1;
RingBuff_t	OutBuffer_1;
RingBuff_t	InBuffer_2;
RingBuff_t	OutBuffer_2;
RingBuff_t	InBuffer_3;
RingBuff_t	OutBuffer_3;

//Timing variables
unsigned long int seconds_sms=0;
unsigned long int seconds_eeprom=0;

//EEPROM storage variable
char EEMEM eeprom_odo[7]={'0'};


//*******************************************************************************
//******************************************************************************* 
//							FUNCTION DECLARATIONS 
//*******************************************************************************
//******************************************************************************* 

//Parent initilisation function
void	Init_All(unsigned int*);

//Timer initialisation function
void	Timer_Init(void);

//Uart setup
void    UART1_Init(void); 
void    UART2_Init(void); 
void    UART3_Init(void); 

//Modem interface functions
void	GSM_Power_On(void);
void	MDM_Init1(void);
void	MDM_Init2(void);
void	MDM_Init3(void);
void	Send_SMS1(long int, char*, char*);
void	Send_SMS2(long int, char*, char*);
void	Send_SMS3(long int, char*, char*);

//OBD interface functions
void	OBD_Init(void);
char*	Get_Vin(char*, char*);
int		Get_Speed(char*);

//EEPROM access
long int	Read_Eeprom(char*, unsigned int);
void		Write_Eeprom(long int, char* , unsigned int);

//Uart string interaction
void	Write_Uart1(char*);
void	Write_Uart2(char*);
void	Write_Uart3(char*);
char*	Read_Uart1(char*);
char*	Read_Uart2(char*);
char*	Read_Uart3(char*);

//*******************************************************************************
//******************************************************************************* 
//									MAIN 
//*******************************************************************************
//******************************************************************************* 

int	main(void)
{
	//startup flag
	unsigned int	init_status=1;
	
	//arrays to handle the incoming UART strings
	char	uart1_input[50]={0};
	char	uart2_input[50]={0};
	char	uart3_input[50]={0};
	
	
	//odometer calculation variables
	int	current_speed=0;
	int	last_speed=0;
	long int		current_odo=0;
	char			current_odo_s[7]="000000";  
	char*			tempstring;
	
	//flag to indicate a recent EEPROM write
	unsigned int	current_speed_stored=0;
	
	//sms message recipient
	char* recipient="+61404088444";
	
	//current fake VIN
	char* vin="666666";
			
	//run all initilisation functions in correct order
	Init_All(&init_status);
	
	
	//debug out
	Write_Uart2("Startup done, main while loop beginning.\r");
	
	
	//read last stored odometer from EEPROM
	current_odo=Read_Eeprom((char*)&eeprom_odo, init_status);
	
	//convert from string to long
	ltoa(current_odo, current_odo_s,10);
	
	//write value to screen
	Write_Uart2("\r\n Current odometer reading is :");
	Write_Uart2(current_odo_s);
	Write_Uart2("\r");
	
	
	//main computation loop
	while(1)
	{
		
		//each SMS interval, send SMS with current odometer ...
		if(seconds_sms>SMS_INTERVAL)
		{
			//current_vin=Get_Vin(uart3_input, current_vin);
			Send_SMS2(current_odo, vin, recipient);
			seconds_sms=0;
		}
		
		//get speed - will wait until register has been filled. speed request is triggered at 1 second intervals
		Write_Uart2("Waiting for speed value good sir.\r");
		itoa(last_speed, tempstring, 10);
		
		Write_Uart2("Last speed value was: ");
		Write_Uart2(tempstring);
		
		//get speed value from OBD interface that has been ISR requested (Timer 1)
		current_speed=Get_Speed(uart3_input);
			
		//new speed is within tolerance of current speed
		if(((abs(current_speed-last_speed))<SPEED_TOLERANCE)==1)
		{		
			//vehicle is moving and odometer needs to be updated
			current_speed_stored=0;
			current_odo=current_odo+current_speed;
			Write_Uart2("Vehicle odo incremented.\r");
			last_speed=current_speed;
			
			//check if EEPOM is overdue for an update (long continuous drivetime)
			if ((seconds_eeprom>EEPROM_MAXPERIOD)==1)
			{
				Write_Eeprom(current_odo, (char*)&eeprom_odo, init_status);	
				seconds_eeprom=0;
				current_speed_stored=1;
				Write_Uart2("Vehicle moving but EEPROM write past maxval. EEPROM updated.\r");
				continue;	//skip rest of while loop and wait for new speed
			}
			
			continue;		//skip rest of while loop and wait for new speed
		}
		
		
		//vehicle is not moving, odometer does not need updating. Write EEPROM if interval greater than min
		if (((current_speed==0)&&(current_speed_stored==0)&&(seconds_eeprom>EEPROM_MINPERIOD))==1)
		{			
			Write_Eeprom(current_odo, (char*)&eeprom_odo, init_status);
			Write_Uart2("Vehicle stationary, EEPROM updated. Odo left alone.\r");
			current_speed_stored=1;
			seconds_eeprom=0;
			continue;		//skip rogue value calculations below and wait for new speed	
		}
		
		//speed value is rogue, ignore latest reading and assume speed is as before
		current_speed_stored=0;
		current_odo=current_odo+last_speed;
			
	}
}

//*******************************************************************************
//******************************************************************************* 
//							INITIALISATION FUNCTIONS 
//*******************************************************************************
//******************************************************************************* 


//===============================================================================
void	Init_All(unsigned int* init_status)
{
	//enable debug LEDs	
	DDRB = 0xff;
	//DDRD = 0x00;	
	
	//turn all LEDs on during init
	PORTB=0xff; 
		
	// initialize the UARTs 
	UART1_Init(); 
	UART2_Init(); 
	UART3_Init();
			
	//enable global interrupts
	sei();
		 
	//Turn on GSM modem
	GSM_Power_On();
   
   	//initialise external hardware
	MDM_Init();
	OBD_Init();
   	
	//change value of init_status to reflect completed initialisation 
	*init_status=1;
	
	//debug
	Write_Uart2("Init complete . . . \r"); 
	
	//inititalise the speed request timer
	Timer_Init();
	
	//Turn off LEDs to signal end of init
	PORTB=0x00;
}

//===============================================================================
void	MDM_Init1(void)
{	
	//turn off modem echo
	Write_Uart1("ATE0\r");
	
	//wait for modem
	_delay_ms(1000);
		
	//compact response form
 	Write_Uart1("ATV0\r");
	 
	 //wait for modem
	_delay_ms(1000);
	 
	//set interface baud rate to match AVR setup
	Write_Uart1("AT+IPR=115200\r");	
	
	//wait for modem	
	_delay_ms(1000);
		
	//set interface style
	Write_Uart1("AT#SELINT=2\r");
	
	//wait for modem	
	_delay_ms(1000);
	
	//set SMS message style	
	Write_Uart1("AT+CMGF=1\r");						
	
	//wait for modem
	_delay_ms(5000);
	
	//request network status
	Write_Uart1("AT+CREG?\r");
		
	//debug
	Write_Uart2("Modem setup done\r");
	
	
}

//===============================================================================
void	MDM_Init2(void)
{
	//turn off modem echo
	Write_Uart2("ATE0\r");
	
	//wait for modem
	_delay_ms(1000);
	
	//compact response form
 	Write_Uart2("ATV0\r");
	 
	 //wait for modem
	_delay_ms(1000);
	 
	//set interface baud rate to match AVR setup
	Write_Uart2("AT+IPR=115200\r");	
	
	//wait for modem	
	_delay_ms(1000);
		
	//set interface style
	Write_Uart2("AT#SELINT=2\r");
	_delay_ms(1000);
	
	//set SMS message style	
	Write_Uart2("AT+CMGF=1\r");						
	
	//wait for modem
	_delay_ms(5000);
	
	//request network status
	Write_Uart2("AT+CREG?\r");
		
	//debug
	Write_Uart2("Modem setup done\r");
}


//===============================================================================
void	MDM_Init3(void)
{
	//turn off modem echo
	Write_Uart3("ATE0\r");
	
	//wait for modem
	_delay_ms(1000);
	
	//compact response form
 	Write_Uart3("ATV0\r");
	 
	 //wait for modem
	_delay_ms(1000);
	 
	//set interface baud rate to match AVR setup
	Write_Uart3("AT+IPR=115200\r");	
	
	//wait for modem	
	_delay_ms(1000);
		
	//set interface style
	Write_Uart3("AT#SELINT=2\r");
	_delay_ms(1000);
	
	//set SMS message style	
	Write_Uart3("AT+CMGF=1\r");						
	
	//wait for modem
	_delay_ms(5000);
	
	//request network status
	/*Write_Uart3("AT+CREG?\r");*/
		
	//debug
	Write_Uart2("Modem setup done\r");
}

void OBD_Init(void)
{
	Write_Uart2("Setting up OBD.\r");
	
	//disable command echo
	Write_Uart3("ATE0\r");
	
	//wait to be ready
	_delay_ms(1000);
	
	//disable linefeed character after response
	Write_Uart3("ATL0\r");
	
	//wait to be ready
	_delay_ms(1000);
	
	//disable headers
	Write_Uart3("ATH0\r");
	
	//wait to be ready
	_delay_ms(1000);
	
	//format ascii
	Write_Uart3("ATFD\r");
	
	//wait to be ready
	_delay_ms(1000);
	
	//send first command to wake up OBD bus
	Write_Uart3("0100\r");
	
	//debug info
	Write_Uart2("OBD setup done.\r");
	
	//wait to be ready
	_delay_ms(5000);
	
}

//===============================================================================
void	Timer_Init(void)
{
	// Configure timer 1 for CTC mode 
	TCCR1B |= (1 << WGM12); 

	// Enable CTC interrupt on timer 1   	
	TIMSK1 |= (1 << OCIE1A); 

   	// Set CTC compare value for 1 second at FCPU Hz AVR clock, with a prescaler of 1024 
	OCR1A   = 10800; 

	// Start timer at Fcpu/1024 
   	TCCR1B |= ((1 << CS10) | (1 << CS12)); 
}

//===============================================================================
void	UART1_Init(void) 
{ 
	// initialise buffer data structures
	RingBuffer_InitBuffer(&InBuffer_1);
	RingBuffer_InitBuffer(&OutBuffer_1);

	//setup UART1 registers
	UCSR1B |= (1 << RXEN1) | (1 << TXEN1);	// Turn on the transmission and reception circuitry 
	UCSR1C |= (1 << UCSZ10) | (1 << UCSZ11);	// Use 8-bit character sizes 
	UBRR1L = UART1_BAUD_PRESCALE;	// Load lower 8-bits of the baud rate value into the low byte of the UBRR register 
	UBRR1H = (UART1_BAUD_PRESCALE >> 8);	// Load upper 8-bits of the baud rate value into the high byte of the UBRR register 

	// enable the Tx Complete interrupt 
    UCSR1B |= ( 1 << TXCIE1 ); 
	  
	// enable the Rx Ready interrupt 
    UCSR1B |= ( 1 << RXCIE1 ); 
} 

//===============================================================================
void	UART2_Init(void) 
{ 
	// initialise buffer data structures
	RingBuffer_InitBuffer(&InBuffer_2);
	RingBuffer_InitBuffer(&OutBuffer_2);

	//setup UART2 registers
	UCSR2B |= (1 << RXEN2) | (1 << TXEN2);	// Turn on the transmission and reception circuitry 
	UCSR2C |= (1 << UCSZ20) | (1 << UCSZ21);	// Use 8-bit character sizes 
	UBRR2L = UART2_BAUD_PRESCALE;	// Load lower 8-bits of the baud rate value into the low byte of the UBRR register 
	UBRR2H = (UART2_BAUD_PRESCALE >> 8);	// Load upper 8-bits of the baud rate value into the high byte of the UBRR register 

	// enable the Tx Complete interrupt 
    UCSR2B |= ( 1 << TXCIE2 ); 
	  
	// enable the Rx Ready interrupt 
    UCSR2B |= ( 1 << RXCIE2 ); 
} 


//===============================================================================
void	UART3_Init(void) 
{ 
	// initialise buffer data structures
	RingBuffer_InitBuffer(&InBuffer_3);
	RingBuffer_InitBuffer(&OutBuffer_3);

	//setup UART2 registers
	UCSR3B |= (1 << RXEN3) | (1 << TXEN3);	// Turn on the transmission and reception circuitry 
	UCSR3C |= (1 << UCSZ30) | (1 << UCSZ31);	// Use 8-bit character sizes 
	UBRR3L = UART3_BAUD_PRESCALE;	// Load lower 8-bits of the baud rate value into the low byte of the UBRR register 
	UBRR3H = (UART3_BAUD_PRESCALE >> 8);	// Load upper 8-bits of the baud rate value into the high byte of the UBRR register 

	// enable the Tx Complete interrupt 
    UCSR3B |= ( 1 << TXCIE3 ); 
	  
	// enable the Rx Ready interrupt 
    UCSR3B |= ( 1 << RXCIE3 ); 
} 


//=============================================================================== 
void	GSM_Power_On(void)
{
	#define 	GSM_3V8				(0x01 << 6)
	#define 	GSM_ONOFF			(0x01 << 6)
	#define 	GSM_RST				(0x01 << 7)
	
	#define     GSM_3V8_ON			(PORTE &= ~GSM_3V8)
	#define     GSM_PWR_OFF			(PORTD &= ~GSM_ONOFF)
	#define     GSM_PWR_ON			(PORTD |= GSM_ONOFF)
	#define     GSM_RST_OFF			(PORTD &= ~GSM_RST)
	
	#define		DDRD_INIT  	( GSM_ONOFF | GSM_RST )
	
	DDRD = DDRD_INIT;
	
	//enable power to modem
	GSM_3V8_ON;
	
	//turn
	GSM_PWR_OFF;
	
	//disable reset
	GSM_RST_OFF;
	
	//wait 0.5 seconds
	_delay_ms(500);
	
	//turn power on
	GSM_PWR_ON;
	
	//debug comment
	Write_Uart2("Holding ON low for 2 seconds\r");
	
	//hold power for 1.5 seconds
	_delay_ms(1500);
	
	//finish powerup pulse
	GSM_PWR_OFF;
	
	//debug 
	Write_Uart2("Done holding ON low for 2 seconds, waiting.\r");
	
	//wait 10 second
	_delay_ms(20000);
	
	Write_Uart2("Done Waiting.\r");
}


int Get_Speed(char* uart3_input)
{
	//OBD response manipulation variables
	char*	char_temp1 = malloc(6);
	char*	char_temp2 = malloc (3);
	int		speed_temp = 0;
	
	//retrieve full response from UART
	char_temp1=Read_Uart3(uart3_input);
	
	//extract character relating to speed
	char_temp2[0]=char_temp1[3];
	char_temp2[1]=char_temp1[4];
	
	//terminate string
	char_temp2[2]='\0';
	
	//convert hex valued string to decimal int
	while(*char_temp2) 
	{ 
		if((*char_temp2>='0')&&(*char_temp2<='9')) 
		{ 
			speed_temp +=(*char_temp2 - '0'); 
		} 
		else if((*char_temp2>='A')&&(*char_temp2<='F')) 
		{ 
			speed_temp+=(*char_temp2-'A'+10); 
		} 
		
		char_temp2++; 
	}
	
	//return extracted int
	return speed_temp;
		
}

void	Send_SMS1(long int odometer, char* current_vin, char* recipient)
{
	//temporary variable for manipulation of odometer
	char*	odometer_string="0000000";
	
	//convert odometer back to km/h from km/s
	odometer=odometer/3600;
	
	//convert int value to string for transmission
	odometer_string=ltoa(odometer, odometer_string,10);
	
	//set recipient of text message	by including defined number
	Write_Uart1("AT+CMGS=");
	Write_Uart1(recipient);
	Write_Uart1("\r");

	//wait for 	modem to be ready
	_delay_ms(2000);
	
	//message contents 
	Write_Uart1("Current VIN is: ");
	Write_Uart1(current_vin);
	Write_Uart1(".");
	Write_Uart1("Current ODO is: ");
	Write_Uart1(odometer_string);
	Write_Uart1(".\x1A"); //insert CNTRL-Z character to terminate message text	
	
	
	//wait for message to be sent, response is not monitored 					
	_delay_ms(2000);
}


void	Send_SMS2(long int odometer, char* current_vin, char* recipient)
{
	//temporary variable for manipulation of odometer
	char*	odometer_string="0000000";
	
	//convert odometer back to km/h from km/s
	odometer=odometer/3600;
	
	//convert int value to string for transmission
	odometer_string=ltoa(odometer, odometer_string,10);
	
	//set recipient of text message	by including defined number
	Write_Uart2("AT+CMGS=");
	Write_Uart2(recipient);
	Write_Uart2("\r");

	//wait for 	modem to be ready
	_delay_ms(2000);
	
	//message contents 
	Write_Uart2("Current VIN is: ");
	Write_Uart2(current_vin);
	Write_Uart2(". ");
	Write_Uart2("Current ODO is: ");
	Write_Uart2(odometer_string);
	Write_Uart2(".\x1A"); //insert CNTRL-Z character to terminate message text	
	
	
	//wait for message to be sent, response is not monitored 					
	_delay_ms(2000);
}

void	Send_SMS3(long int odometer, char* current_vin, char* recipient)
{
	//temporary variable for manipulation of odometer
	char*	odometer_string="0000000";
	
	//convert odometer back to km/h from km/s
	odometer=odometer/3600;
	
	//convert int value to string for transmission
	odometer_string=ltoa(odometer, odometer_string,10);
	
	//set recipient of text message	by including defined number
	Write_Uart3("AT+CMGS=");
	Write_Uart3(recipient);
	Write_Uart3("\r");

	//wait for 	modem to be ready
	_delay_ms(2000);
	
	//message contents 
	Write_Uart3("Current VIN is: ");
	Write_Uart3(current_vin);
	Write_Uart3(". ");
	Write_Uart3("Current ODO is: ");
	Write_Uart3(odometer_string);
	Write_Uart3(".\x1A"); //insert CNTRL-Z character to terminate message text	
	
	
	//wait for message to be sent, response is not monitored 					
	_delay_ms(2000);
}

//*******************************************************************************
//******************************************************************************* 
//								EEPROM FUNCTIONS 
//*******************************************************************************
//******************************************************************************* 


//===============================================================================
long int	Read_Eeprom(char* eemom_odo, unsigned int init_status)
{
	//temporary variables to retrieve the stored unscaled odometer reading
	long int	temp_odo_int=0;
	char		temp_odo_char[7]={'0'};
	
	
	//disable all interrupts to ensure EEPROM write is not disturbed	
	cli();
		
	//retrieve data from EEPROM
	eeprom_read_block((void*)&temp_odo_char, (const void*)eeprom_odo,7); 		
	
	//convert string to an int to enable further calculations
	temp_odo_int=atol(temp_odo_char);
		
	
	//re enable global interrupts only if this is not during the initialisation
	//global interrupt enable is done in main once inititalisation is complete
	if(init_status==1)
	{
		sei();
	}
	
	//return EEPROM stored value as an int to be added to in main
	return temp_odo_int;
}

//===============================================================================
void	Write_Eeprom(long int temp_odo_int, char* eemom_odo, unsigned int init_status)
{
	//temp variable to store the unscaled odometer reading
	char*	temp_odo_char="bargebarge";
		
	//disable all interrupts to ensure EEPROM write is not disturbed	
	cli();
	
	//convert current unscaled odometer to a string
	ltoa(temp_odo_int,temp_odo_char,10);
	
	//write string to EEPROM
	eeprom_write_block((const void*)temp_odo_char,(void*)eeprom_odo,7); 
		
	//re enable global interrupts only if this is not during the initialisation
	//global interrupt enable is done in main once inititalisation is complete
	if(init_status==1)
	{
		sei();
	}	
}


//===============================================================================
void	Write_Uart1(char * uart1_message)
{
	int i=0, count=0;
	//char uart_message;
	
 	while((Uart1OutSendingInProgress==1))
	{
 		UDR2='X';//Do nothing and wait
  		_delay_ms(1000);
	}

	while((RingBuffer_IsEmpty(&OutBuffer_1)==0))
	{
 		UDR2='Y';//Do nothing and wait
 		_delay_ms(1000);
	}
	
	//disable transmit interrupt to buffer is not emptied before being filled appropriately
	UCSR1B &= ~( 1 << TXCIE1 ); 
	
	//grab length of string	
    count = strlen(uart1_message);	

	//enable sending flag so that previous transmission gets completed before beginning new one
	Uart1OutSendingInProgress=1;
			
	
	//fill buffer with all except first character (first will be sent directly to UART port to start transmit)				
	for(i=1; i<count ; i++)
		{												
			RingBuffer_Insert(&OutBuffer_1, uart1_message[i]);
		}	
				
	// enable the Tx Complete interrupt 
	UCSR1B |= ( 1 << TXCIE1 ); 
	  		
	//send first character directly to UART to kick off transmit
	UDR1=uart1_message[0];
}


//===============================================================================
char*	Read_Uart1(char* uart1_input)
{
	//variables used to manage character
	int count=0;
	int i=0;

// 	//wait while reception of uart is in progress
//  	while((Uart1InReceivingInProgress==1)||((RingBuffer_IsEmpty(&InBuffer_1))==1)==1)
//  	{
// 		Write_Uart2("Waiting for modem response to begin and complete.\r");
// 		_delay_ms(1000);
// 		//do nothing and wait for reception to begin, be processed and complete
//  	}
	 
	 	while((Uart1InReceivingInProgress==1))
	{
		UDR2='A';//Do nothing and wait
		_delay_ms(1000);
	}

	while((RingBuffer_IsEmpty(&InBuffer_1)==1))
	{
		UDR2='B';//Do nothing and wait
		_delay_ms(1000);
	}
	
	//get number of characters in buffer
	count = (RingBuffer_GetCount(&InBuffer_1));	
	 
	//remove characters from buffer and put them into a string
	for(i=0; i<count ; i++)
		{
			if((RingBuffer_IsEmpty(&InBuffer_1))==0)	//making sure buffer ISN'T empty first
			{
				uart1_input[i]=RingBuffer_Remove(&InBuffer_1);
			}
			else
			{
				break;
			}			
		
		}
		
	//RingBuffer_InitBuffer(&InBuffer_1);
	return uart1_input;
}

//===============================================================================
void	Write_Uart2(char * uart2_message)
{
	int i=0, count=0;
	//char uart_message;
	
 	while((Uart2OutSendingInProgress==1))
	{
// 		UDR2='X';//Do nothing and wait
//  		_delay_ms(100);
	}

	while((RingBuffer_IsEmpty(&OutBuffer_2)==0))
	{
// 		UDR2='Y';//Do nothing and wait
// 		_delay_ms(100);
	}
	
	//disable transmit interrupt to buffer is not emptied before being filled appropriately
	UCSR2B &= ~( 1 << TXCIE2 ); 
	
	//grab length of string	
    count = strlen(uart2_message);	

	//enable sending flag so that previous transmission gets completed before beginning new one
	Uart2OutSendingInProgress=1;
			
	
	//fill buffer with all except first character (first will be sent directly to UART port to start transmit)				
	for(i=1; i<count ; i++)
		{												
			RingBuffer_Insert(&OutBuffer_2, uart2_message[i]);
		}	
				
	// enable the Tx Complete interrupt 
	UCSR2B |= ( 1 << TXCIE2 ); 
	  		
	//send first character directly to UART to kick off transmit
	UDR2=uart2_message[0];
}


//===============================================================================
char*	Read_Uart2(char* uart2_input)
{
	//variables used to manage character
	int count=0;
	int i=0;
	
// 		//wait while reception of uart is in progress
  	//while((Uart2InReceivingInProgress==1)||(RingBuffer_IsEmpty(&InBuffer_2))==1);
//  	{
// 		Write_Uart2("Waiting for speed to arrive and complete.\r");
// 		_delay_ms(1000);
// 		//do nothing and wait for reception to begin, be processed and complete
//  	}
	 

	while((Uart2InReceivingInProgress==1)||(RingBuffer_IsEmpty(&InBuffer_2)==1))
	 {
		_delay_ms(1);	
	 }		 

	
	//get number of characters in buffer
	count = (RingBuffer_GetCount(&InBuffer_2));	
	 
	//remove characters from buffer and put them into a string
	for(i=0; i<count; i++)
		{
			if((RingBuffer_IsEmpty(&InBuffer_2))==0)	//making sure buffer ISN'T empty first
			{
				uart2_input[i]=RingBuffer_Remove(&InBuffer_2);
			}
			else
			{
				break;
			}			
		}
		
	for(i=count;i<50;i++)
		{
			uart2_input[i]='\0';
		}		
		
	
	RingBuffer_InitBuffer(&InBuffer_2);
	
	return uart2_input;
}

//===============================================================================
char*	Read_Uart3(char* uart3_input)
{
	//variables used to manage characters
	int count=0,  
	i=0;
	char* tempstring;
		
	//wait while reception of uart is in progress
 	while(Uart3InReceivingInProgress==1)
 	{
		Write_Uart2("Receiving in progress.\r");
		_delay_ms(500);
 	}
	 
	
	 
	 while((RingBuffer_IsEmpty(&InBuffer_3))==1)
 	{
		Write_Uart2("InBuffer_3 is empty.\r");
		_delay_ms(500);
	count = (RingBuffer_GetCount(&InBuffer_3));
	count++;
	 itoa(count, tempstring, 10);
	 Write_Uart2(tempstring);
	 _delay_ms(500);
 	}
	 
// while((Uart3InReceivingInProgress==1)||(RingBuffer_IsEmpty(&InBuffer_3)==1))
// 	 {
// 		_delay_ms(500);	
// 		Write_Uart2("Waiting for speed,\r");
// 	 }	
// 	 


	//get number of characters in buffer
	count = (RingBuffer_GetCount(&InBuffer_3));	

	//remove characters from buffer and put them into a string
	for(i=0; i<count ; i++)
		{
			if((RingBuffer_IsEmpty(&InBuffer_3))==0)	//making sure buffer ISN'T empty first
			{
				uart3_input[i]=RingBuffer_Remove(&InBuffer_3);
			}
			else
			{
				break;
			}			
		
		}
		
	for(i=count;i<50;i++)
		{
			uart3_input[i]='\0';
		}		

	RingBuffer_InitBuffer(&InBuffer_3);
	
	return uart3_input;
}




//===============================================================================
void	Write_Uart3(char * uart3_message)
{
	
	int i=0, count=0;
	
	while((Uart3OutSendingInProgress==1)||(RingBuffer_IsEmpty(&OutBuffer_3)==0))
	{
		//Write_Uart2("Waiting to write to UART 3. Buffer is either empty or UART is still sending another message.")	;
		_delay_ms(100);	//Do nothing and wait
	}

	//disable transmit interrupt to buffer so it is not emptied before being filled with complete string
	UCSR3B &= ~( 1 << TXCIE3 ); 
	
	//enable sending flag so that previous transmission gets completed before beginning new one
	Uart3OutSendingInProgress=1;

	//grab length of string	
    count = strlen(uart3_message);	

	//fill buffer with all except first character (first will be sent directly to UART port to initiate transmit)				
	for(i=1; i<count ; i++)
		{												
			RingBuffer_Insert(&OutBuffer_3, uart3_message[i]);
		}	
				
	// enable the Tx Complete interrupt 
	UCSR3B |= ( 1 << TXCIE3 ); 
	  		
	//send first character directly to UART to kick off transmit
	UDR3=uart3_message[0];
}

//*******************************************************************************
//******************************************************************************* 
//								INTERRUPT HANDLERS 
//*******************************************************************************
//******************************************************************************* 


//===============================================================================
	ISR(TIMER1_COMPA_vect)
{
	//ISR is triggered every second and requests vehicle speed from OBD interface on UART3

	//"vehicle speed" PID tranmission	
	Write_Uart3("010D\r");
	
	//increment counter for SMS interval
	seconds_sms++;		
	
	//increment timer for EEPROM writes
	seconds_eeprom++;
}

//===============================================================================
	ISR (USART1_TX_vect) 
{ 	
	// This interrupt service routine is called when a byte has been sent through the
	// UART1 port, and it's ready to receive the next byte for transmission. 
	// If there is not another byte to write, then clear the "UartOutSendingInProgress" 
	// flag, otherwise set it if a byte has just been sent. 

	if (RingBuffer_IsEmpty(&OutBuffer_1)==1)  // if buffer is empty 
	{ 
		Uart1OutSendingInProgress = 0;  // clear sending in progress flag
		return;							// transmission complete, exit ISR
	} 
	else
	{
		Uart1OutSendingInProgress = 1;	//enable sending in progress flag
		UDR1 = RingBuffer_Remove(&OutBuffer_1); //remove character from buffer and send to UART output
	}	
} 


//===============================================================================
	ISR (USART1_RX_vect) 
{ 			
	// This interrupt service routine is called when a byte has been received the
	// UART1 port, and it's ready to be read
	
	char c;
	c=UDR1;
		
	//if incoming character is a carriage return	
	if (c=='\r')
	{
		Uart1InReceivingInProgress=0;	// clear sending in progress flag
		return;							// transmission complete, exit ISR
	}
	
	if(c=='\n')
	{
		//linefeed character received, ignore
		return;
	}
	else
	{ 
		Uart1InReceivingInProgress=1;
		RingBuffer_Insert(&InBuffer_1, c) ;
	}		   	 
} 

//===============================================================================
	ISR (USART2_TX_vect) 
{ 	
	// This interrupt service routine is called when a byte has been sent through the
	// UART2 port, and it's ready to receive the next byte for transmission. 
	// If there is not another byte to write, then clear the "UartOutSendingInProgress" 
	// flag, otherwise set it if a byte has just been sent. 

	if (RingBuffer_IsEmpty(&OutBuffer_2)==1)  // if buffer is empty 
	{ 
		Uart2OutSendingInProgress = 0;  // clear sending in progress flag
		return;							// transmission complete, exit ISR
	} 
	else
	{
		Uart2OutSendingInProgress = 1;	//enable sending in progress flag
		UDR2 = RingBuffer_Remove(&OutBuffer_2); //remove character from buffer and send to UART output
	}	
} 


//===============================================================================
	ISR (USART2_RX_vect) 
{ 			
	// This interrupt service routine is called when a byte has been received the
	// UART2 port, and it's ready to be read
	
	char c;
	c=UDR2;
		
	//if incoming character is a carriage return	
	if (c=='\r')
	{
		Uart2InReceivingInProgress=0;	// clear sending in progress flag
		return;							// transmission complete, exit ISR
	}
	else
	{ 
		Uart2InReceivingInProgress=1;
		RingBuffer_Insert(&InBuffer_2, c) ;
		uart2_count++;
	}		   	 
} 



//===============================================================================
	ISR (USART3_TX_vect) 
{ 	
	// This interrupt service routine is called when a byte has been sent through the
	// UART1 port, and it's ready to receive the next byte for transmission. 
	// 

	// If there is not another byte to write, then clear the "UartSendingInProgress" 
	// flag, otherwise set it if a byte has just been sent. 
	// 

	if (RingBuffer_IsEmpty(&OutBuffer_3)==1)  // if buffer is empty 
	{ 
		Uart3OutSendingInProgress = 0;   // clear "sending in progress" flag 
		return; // then we have nothing to do, so return 
	} 
	else
	{
		Uart3OutSendingInProgress = 1;	//raise send in progress flag
		//remove character from buffer and send to UART output
		UDR3 = RingBuffer_Remove(&OutBuffer_3);
   	}	
} 


//===============================================================================
	ISR (USART3_RX_vect) 
{ 			
	// This interrupt service routine is called when a byte has been received the
	// UART1 port, and it's ready to be read
	
	
	char c;
	c=UDR3;
	
	
	//check for prompt to confirm end of response
	if (c==0x3e)
	{
		Uart3InReceivingInProgress=0;	//reception of string finished
		return;							//exit ISR
	}
		
	//reject control characters	present in OBD response
	if((c>0x39) || (c<0x30))
	{
		Uart3InReceivingInProgress=1;		//reception of string 
		return;
	}
	else
	{ 
		Uart3InReceivingInProgress=1;		//reception of string continuing
		RingBuffer_Insert(&InBuffer_3, c) ;	//place character into buffer
	}
		

}		   	 
	
	