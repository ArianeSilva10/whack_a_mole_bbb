#include "gpio.h"
#include "timers.h"
#include "uart.h"
#include "lcd.h"

/* Constants */
#define MAX_PERIOD          (1000)
#define MAX_TIMEOUT         (15000)
#define PERIOD_MODIFIER     (50)
#define TIMEOUT_MODIFIER    (500)
#define POLLING_READS       (10)
#define READ_DELAY_TIME     (MAX_PERIOD / POLLING_READS)
#define SEED_INCREMENT      (2)
#define MAX_TRANSITION_TIME (1000)
#define NUMBER_LEVELS       (10)
#define NUMBER_LIVES        (3)
#define NO_BUTTON_PRESSED   (-1)

#define HEART_CHAR          ((const int8_t)0)
#define MOLE_CHAR           ((const int8_t)1)
#define CHECK_CHAR          ((const int8_t)2)
#define SKULL_CHAR          ((const int8_t)3)

/* Maps */
const gpio_handle_t leds[] = {
     {
          .port = GPIO1,
          .pin_number = 28
     },
     {
          .port = GPIO2,
          .pin_number = 1
     },
     {
          .port = GPIO1,
          .pin_number = 29
     },
};

const gpio_handle_t buttons[] =  {
     {
          .port = GPIO3,
          .pin_number = 19
     },
     {
          .port = GPIO0,
          .pin_number = 26
     },
     {
          .port = GPIO3,
          .pin_number = 21
     },
};

/* Constant Macros */
#define NUMBER_LEDS               (sizeof(leds) / sizeof(gpio_handle_t))
#define NUMBER_BUTTONS            (sizeof(buttons) / sizeof(gpio_handle_t))

/* Access Macros */
#define WRITE_LED(i, v)           (gpioSetPinValue(&leds[i], v))
#define READ_BUTTON(i)            (!gpioGetPinValue(&buttons[i]))
#define GET_NEXT_LED()            (rand() % NUMBER_LEDS)
#define CALCULATE_PTS(lvl, t)     ((lvl*10) + (t)/1000)


const uint8_t heart[8] = {
  0b00000,
  0b01010,
  0b11111,
  0b11111,
  0b01110,
  0b00100,
  0b00000,
  0b00000
};

const uint8_t mole[8] = {
  0b01110,
  0b11111,
  0b10101,
  0b11111,
  0b10001,
  0b11111,
  0b11111,
  0b00000
};

const uint8_t check[8] = {
  0b00000,
  0b00001,
  0b00011,
  0b10110,
  0b11100,
  0b01000,
  0b00000,
  0b00000
};

const uint8_t skull[8] = {
  0b00000,
  0b01110,
  0b10101,
  0b11011,
  0b01110,
  0b01110,
  0b00000,
  0b00000
};

/* FSM States */
typedef enum {
    /* Startup Mode */
    STARTUP,

    /* Game Flow */
    LEVEL_SETUP,
    LED_CHOOSE,
    WAIT_INPUT,

    /* Input Related */
    CORRECT_INPUT,
    WRONG_INPUT,

    /* Game Decision */
    VICTORY,
    DEFEAT,

    /* Others */
    TIMEOUT
} states_t;

/* Control Variables */
states_t state;
int lives;
int level;
int seed;
int period;
int timeout;
int points;
int current;
int timeout_counter;
bool update_lcd;

/* System Variables */
lcd_handler_t lcd;

// =============================================================================
// PROTÓTIPOS DE FUNÇÕES
// =============================================================================

void drvComponentInit(void);

void finiteStateMachine(void);

void UpdateLevelDisplay(void);

int intToString(int32_t value, char *buffer, uint8_t size);

// =============================================================================
// CÓDIGO PRINCIPAL
// =============================================================================

int main(void){
    IntMasterIRQDisable();

    drvComponentInit();
     
    IntMasterIRQEnable();

    /* INICIALIZAÇÃO DAS VARIÁVEIS */
    state = STARTUP;
    lives = 3;
    level = 1;
    seed = 0;
    period = 0;
    timeout = 0;
    points = 0;
    current = 0;
    timeout_counter = 0;
    update_lcd = true;

    while(1) {
        finiteStateMachine();
    }
}

// =============================================================================
// IMPLEMENTAÇÃO DAS FUNÇÕES
// =============================================================================

static unsigned int next = 1;

void srand(unsigned int seed) {
    next = seed;
}

int rand(void) {
    next = next * 1103515245 + 12345;
    return (unsigned int)(next / 65536) % 32768;
}

void drvLcdInit(void) {
     gpio_handle_t rs;
     rs.port = GPIO1;
     rs.pin_number = 12;
     gpioPInitPin(&rs, OUTPUT);

     gpio_handle_t en;
     en.port = GPIO1;
     en.pin_number = 13;
     gpioPInitPin(&en, OUTPUT);

     gpio_handle_t d4;
     d4.port = GPIO1;
     d4.pin_number = 14;
     gpioPInitPin(&d4, OUTPUT);

     gpio_handle_t d5;
     d5.port = GPIO1;
     d5.pin_number = 15;
     gpioPInitPin(&d5, OUTPUT);

     gpio_handle_t d6;
     d6.port = GPIO1;
     d6.pin_number = 16;
     gpioPInitPin(&d6, OUTPUT);

     gpio_handle_t d7;
     d7.port = GPIO1;
     d7.pin_number = 17;
     gpioPInitPin(&d7, OUTPUT);

     lcd.rs = rs;
     lcd.en = en;
     lcd.data[0] = d4;
     lcd.data[1] = d5;
     lcd.data[2] = d6;
     lcd.data[3] = d7;

     lcdInitModule(&lcd);
}

void drvComponentInit(void) {
    IntDisableWatchdog();

    gpioInitModule(GPIO0);
    gpioInitModule(GPIO1);
    gpioInitModule(GPIO2);
    gpioInitModule(GPIO3);
    timerInitModule();
    drvLcdInit();

    /* LEDs setup */
    for (int i = 0; i < NUMBER_LEDS; i++) {
        gpioPInitPin(&leds[i], OUTPUT);
    }

    /* Buttons setup */
    for (int i = 0; i < NUMBER_BUTTONS; i++) {
        gpioPInitPin(&buttons[i], INPUT);
        gpioConfigPull(&buttons[i], PULLUP);
    }

    lcdCreateChar(&lcd, HEART_CHAR, heart);
    lcdCreateChar(&lcd, MOLE_CHAR, mole);
    lcdCreateChar(&lcd, CHECK_CHAR, check);
    lcdCreateChar(&lcd, SKULL_CHAR, skull);
}

int PollButtons() {
    for (int i = 0; i < NUMBER_BUTTONS; i++) {
        if (READ_BUTTON(i)) {
            return i;
        }
    }
    return NO_BUTTON_PRESSED;
}

int TurnOnLed(int led) {
    for (int i = 0; i < NUMBER_LEDS; i++) {
        if (i == led) {
            WRITE_LED(i, HIGH);
        } else {
            WRITE_LED(i, LOW);
        }
    }
}

int WriteAllLeds(int value) {
    for (int i = 0; i < NUMBER_LEDS; i++) {
        WRITE_LED(i, value);
    }
}

void finiteStateMachine(void) {
    switch (state) {
        case STARTUP:
            putString("STARTUP\n\r", 10);
            if (update_lcd) {
                lcdClearDisplay(&lcd);
                lcdSetCursor(&lcd, 0, 0);
                lcdWriteString(&lcd, "  Whack'A Mole  ");
                lcdSetCursor(&lcd, 1, 0);
                lcdWriteString(&lcd, " Press a button ");
                update_lcd = false;
            }
            /* Output Logic */
            level = 1;
            lives = NUMBER_LIVES;
            seed += SEED_INCREMENT;

            TurnOnLed(seed/SEED_INCREMENT % NUMBER_LEDS);

            /* Transition Logic */
            if (PollButtons() != NO_BUTTON_PRESSED) {
                update_lcd = true;
                state = LEVEL_SETUP;

            }
            /* Time to wait */
            delay_ms(READ_DELAY_TIME);
            break;

        case LEVEL_SETUP:
            putString("SETUP\n\r", 8);

            //LCD Logic
            if (update_lcd) {
                UpdateLevelDisplay();
                update_lcd = false;
            }

            /* Output Logic */
            period = MAX_PERIOD - PERIOD_MODIFIER * level;
            timeout = MAX_TIMEOUT - TIMEOUT_MODIFIER * level;
            timeout_counter = 0;
            srand(seed);

            state = LED_CHOOSE;
            
            break;

        case LED_CHOOSE:
            putString("LED\n\r", 6);
            current = GET_NEXT_LED();

            TurnOnLed(current);

            state = WAIT_INPUT;
            break;

        case WAIT_INPUT:
            putString("WAIT\n\r", 7);

            for (int i = 0; i < period; i += period / POLLING_READS) {
                int button_pressed = PollButtons();
                
                putString("PRESSED: ", 10);
                putCh((button_pressed % 10) + '0');


                putString("\n\rCURRENT: ", 12);
                putCh((current % 10) + '0');
                putString("\n\r", 3);

                if (button_pressed == current) {
                    state = CORRECT_INPUT;
                  	break;
                } else if (timeout_counter >= timeout) {
                    state = TIMEOUT;
                  	break;
                } else if (button_pressed != NO_BUTTON_PRESSED) {
                    state = WRONG_INPUT;
                  	break;
                } else {
                  	state = LED_CHOOSE;
                }

                delay_ms(period / POLLING_READS);
                timeout_counter += period / POLLING_READS;
            }

            break;

        case CORRECT_INPUT:
        {
            WriteAllLeds(HIGH);

            int level_points = CALCULATE_PTS(level, timeout - timeout_counter);

            lcdClearDisplay(&lcd);

            lcdSetCursor(&lcd, 0, 0);
            lcdWriteChar(&lcd, CHECK_CHAR);
            lcdWriteString(&lcd, "  BOOYAH!!!!  ");
            lcdWriteChar(&lcd, CHECK_CHAR);

            lcdSetCursor(&lcd, 1, 0);
            lcdWriteString(&lcd, "Gained +");

            lcdWriteChar(&lcd, ((level_points / 1000) % 10) + '0');
            lcdWriteChar(&lcd, ((level_points / 100) % 10) + '0'); 
            lcdWriteChar(&lcd, ((level_points / 10) % 10) + '0');
            lcdWriteChar(&lcd, (level_points % 10) + '0');

            lcdWriteString(&lcd, "pts ");

            points += level_points;
            level += 1;
            update_lcd = true;
          
           if (level > NUMBER_LEVELS) {
                state = VICTORY;
            } else {
                state = LEVEL_SETUP;
            }

            delay_ms(MAX_TRANSITION_TIME);
            break;
        }

        case WRONG_INPUT:
            WriteAllLeds(LOW);


            lcdClearDisplay(&lcd);

            lcdSetCursor(&lcd, 0, 0);
            lcdWriteChar(&lcd, SKULL_CHAR);
            lcdWriteString(&lcd, " YOU MISSED!! ");
            lcdWriteChar(&lcd, SKULL_CHAR);

            lcdSetCursor(&lcd, 1, 0);
            lcdWriteString(&lcd, "    Lost a ");
            lcdWriteChar(&lcd, HEART_CHAR);
            lcdWriteString(&lcd, "    ");

            update_lcd = true;

            lives -= 1;

            if(lives == 0) {
              state = DEFEAT;
            }
            else {
              state = LEVEL_SETUP;
            }

            delay_ms(MAX_TRANSITION_TIME);
            break;

        case TIMEOUT:  
            WriteAllLeds(LOW);

            lcdClearDisplay(&lcd);

            lcdSetCursor(&lcd, 0, 0);
            lcdWriteChar(&lcd, SKULL_CHAR);
            lcdWriteChar(&lcd, SKULL_CHAR);
            lcdWriteString(&lcd, " TIME-OUT!! ");
            lcdWriteChar(&lcd, SKULL_CHAR);
            lcdWriteChar(&lcd, SKULL_CHAR);

            lcdSetCursor(&lcd, 1, 0);
            lcdWriteString(&lcd, "    Lost a ");
            lcdWriteChar(&lcd, HEART_CHAR);
            lcdWriteString(&lcd, "    ");

            update_lcd = true;

            state = STARTUP;
            lives -= 1;
            
            if(lives == 0) {
              state = DEFEAT;
            }
            else {
              state = LEVEL_SETUP;
            }

            delay_ms(MAX_TRANSITION_TIME);
            break;

        case DEFEAT: 
            WriteAllLeds(LOW);

            lcdClearDisplay(&lcd);

            lcdSetCursor(&lcd, 0, 0);
            lcdWriteChar(&lcd, SKULL_CHAR);
            lcdWriteChar(&lcd, SKULL_CHAR);
            lcdWriteString(&lcd, " YOU LOST!! ");
            lcdWriteChar(&lcd, SKULL_CHAR);
            lcdWriteChar(&lcd, SKULL_CHAR);

            lcdSetCursor(&lcd, 1, 0);
            lcdWriteString(&lcd, " Score: ");
            lcdWriteChar(&lcd, ((points / 1000) % 10) + '0');
            lcdWriteChar(&lcd, ((points / 100) % 10) + '0'); 
            lcdWriteChar(&lcd, ((points / 10) % 10) + '0');
            lcdWriteChar(&lcd, (points % 10) + '0');

            lcdWriteString(&lcd, "pts ");

            update_lcd = true;

            state = STARTUP;

            delay_ms(MAX_TRANSITION_TIME);
            break;

        case VICTORY:  
            WriteAllLeds(HIGH);

            lcdClearDisplay(&lcd);

            lcdSetCursor(&lcd, 0, 0);
            lcdWriteChar(&lcd, CHECK_CHAR);
            lcdWriteChar(&lcd, CHECK_CHAR);
            lcdWriteString(&lcd, " YOU WON!!! ");
            lcdWriteChar(&lcd, CHECK_CHAR);
            lcdWriteChar(&lcd, CHECK_CHAR);

            lcdSetCursor(&lcd, 1, 0);
            lcdWriteString(&lcd, " Score: ");
            lcdWriteChar(&lcd, ((points / 1000) % 10) + '0');
            lcdWriteChar(&lcd, ((points / 100) % 10) + '0'); 
            lcdWriteChar(&lcd, ((points / 10) % 10) + '0');
            lcdWriteChar(&lcd, (points % 10) + '0');

            lcdWriteString(&lcd, "pts ");

            update_lcd = true;

            state = STARTUP;

            delay_ms(MAX_TRANSITION_TIME);
            break;
    }
}

void UpdateLevelDisplay(){
    lcdClearDisplay(&lcd);

    lcdSetCursor(&lcd, 0, 0);
     
    lcdWriteString(&lcd, "  Whack'A Mole  ");
    
    lcdSetCursor(&lcd, 1, 0);

    /* Level */

    lcdWriteChar(&lcd, MOLE_CHAR);
    lcdWriteChar(&lcd, 'x');
    lcdWriteChar(&lcd, (level / 10) + '0');
    lcdWriteChar(&lcd, (level % 10) + '0');

    /* Separator */
    lcdWriteChar(&lcd,  ' ');

    lcdWriteChar(&lcd, HEART_CHAR);
    lcdWriteChar(&lcd, 'x');
    lcdWriteChar(&lcd, lives + '0');

    /* Separator */
    lcdWriteChar(&lcd, ' ');

    /* Level */
    lcdWriteChar(&lcd, ((points / 1000) % 10) + '0');
    lcdWriteChar(&lcd, ((points / 100) % 10) + '0'); 
    lcdWriteChar(&lcd, ((points / 10) % 10) + '0');
    lcdWriteChar(&lcd, (points % 10) + '0');
    lcdWriteString(&lcd, "pts");


}

int intToString(int32_t value, char *buffer, uint8_t size) {
     char string[size];
     int i;
     for(i = 0; i < size - 1; i++) {
          string[i] = '0' + value % 10;
          value /= 10;
          if(value == 0) {
               break;
          }
     }
     int j;
     int a = i;
     for(j = 0; j <= i; j++) {
          buffer[j] = string[a--];
     }
     buffer[j++] = '\0';
     return j;
}
