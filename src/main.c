#include "gpio.h"
#include "timers.h"
#include "uart.h"

/* Constants */
#define MAX_PERIOD          (1000)
#define MAX_TIMEOUT         (15000)
#define PERIOD_MODIFIER     (200)
#define TIMEOUT_MODIFIER    (2000)
#define POLLING_READS       (10)
#define READ_DELAY_TIME     (MAX_PERIOD / POLLING_READS)
#define SEED_INCREMENT      (2)
#define MAX_TRANSITION_TIME (1000)
#define NUMBER_LEVELS       (10)
#define NUMBER_LIVES        (3)
#define NO_BUTTON_PRESSED   (-1)

/* Maps */
const gpio_handle_t leds[] = {
     {
          .port = GPIO1,
          .pin_number = 12
     },
     {
          .port = GPIO1,
          .pin_number = 13
     },
     {
          .port = GPIO1,
          .pin_number = 14
     },
};

const gpio_handle_t buttons[] =  {
     {
          .port = GPIO1,
          .pin_number = 15
     },
     {
          .port = GPIO1,
          .pin_number = 16
     },
     {
          .port = GPIO1,
          .pin_number = 17
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

// =============================================================================
// PROTÓTIPOS DE FUNÇÕES
// =============================================================================

void drvComponentInit(void);

void finiteStateMachine(void);

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

void drvComponentInit(void) {
     IntDisableWatchdog();

     gpioInitModule(GPIO1);
     timerInitModule();
     
     /* LEDs setup */
     for (int i = 0; i < NUMBER_LEDS; i++) {
          gpioPInitPin(&leds[i], OUTPUT);
     }

     /* Buttons setup */
     for (int i = 0; i < NUMBER_BUTTONS; i++) {
          gpioPInitPin(&buttons[i], INPUT);
          gpioConfigPull(&buttons[i], PULLUP);
     }
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

void finiteStateMachine(void) {
    switch (state) {
        case STARTUP:
            /* Output Logic */
            level = 1;
            lives = NUMBER_LIVES;
            seed += SEED_INCREMENT;

            TurnOnLed(seed/SEED_INCREMENT % NUMBER_LEDS);

            /* Transition Logic */
            if (PollButtons() != NO_BUTTON_PRESSED) {
                state = LEVEL_SETUP;
            }

            /* Time to wait */
            delay_ms(READ_DELAY_TIME);
            break;

        case LEVEL_SETUP:
            /* Output Logic */
            period = MAX_PERIOD - PERIOD_MODIFIER * level;
            timeout = MAX_TIMEOUT - TIMEOUT_MODIFIER * level;
            timeout_counter = 0;
            srand(seed);

            state = LED_CHOOSE;
            
            break;

        case LED_CHOOSE:
            current = GET_NEXT_LED();

            TurnOnLed(current);

            state = WAIT_INPUT;
            break;

        case WAIT_INPUT:

            for (int i = 0; i < period; i += period / POLLING_READS) {
                int button_pressed = PollButtons();

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

            points += level_points;
            level += 1;
          
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

            state = STARTUP;

            delay_ms(MAX_TRANSITION_TIME);
            break;

        case VICTORY:  
            WriteAllLeds(HIGH);

            state = STARTUP;

            delay_ms(MAX_TRANSITION_TIME);
            break;
    }
}