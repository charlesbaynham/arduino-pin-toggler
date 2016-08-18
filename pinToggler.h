/* 
arduino-pin-toggler
===================

### Copyright Charles Baynham 2016

This Arduino library manages the toggling of arbitary pins at controllable rates. 
For example, it can be used to flash an LED at different speeds according
to the state of your device. 

It works using interrupts and is designed to be lightweight when running,
so will not interfere with existing code. 

This library requires exclusive usage of TIMER1 so is incompatible with
libraries that also use this timer. 

Usage
-----

Include the class by adding `#include <pinToggler.h>` to your sketch.

Init the class by passing it an array of pins that will be toggled. E.g.

  int pins[3] = {13, A4, A5};
  pinToggler<3>::init(pins);

The template parameter (`<3>` above) defines the total number of pins being controlled.

These pins will start `LOW`, not toggling. 

To start the toggling, set their speed to `OFF`, `SLOW`, `MEDIUM`, `FAST` or `MAX`. E.g.

  pinToggler<3>::setFlashRate(0, SLOW);

N.B. The template parameter (here `<3>`) must match the one used in `init()` or this will
throw an error. Also the LED parameter in `setFlashRate(LED, rate)` refers to the pins passed to `init()`, zero-indexed in the order that they appeared in in the array.

The speeds refer to fractions of the max speed, defined by `FLASH_FREQ_HZ`.

Note that all the function calls here are static members of the class. Although a class object
is created, this happens internally. For memory management purposes, be aware that this class 
allocates `3 * numPins` bytes on the heap. Thus, to avoid memory framentation, `init()` should be called
as early in your code as possible. 
*/

#define PRESCALER 1024 // If this is changed, you must also update this value in the init() method

// How often the ISR will trigger, defining the max toggle rate
#define FLASH_FREQ_HZ 8

enum FLASHRATE {
  OFF = 0,
  SLOW = 1,
  MEDIUM = 2,
  FAST = 4,
  MAX = 8
};

// Declare the TIMER1 ISR routine
extern "C" void TIMER1_OVF_vect(void) __attribute__ ((signal));

class pinTogglerBase {

  // The global, shared instance
  static pinTogglerBase * _instance;

  // Define a value for timer1_counter such that our ISR is called at the right rate
  static const int timer1_counter = 65536 - 16000000/PRESCALER/FLASH_FREQ_HZ;   // preload timer 65536-16MHz/256/2Hz

  // Allow the ISR to access this
  friend void TIMER1_OVF_vect();

public:

  // Return the number of pins defined by the templated derived class
  virtual int numDerivedPins() = 0;

  // Get the global instance
  inline static pinTogglerBase * instance() { return _instance; }
  
  // Do the loop. This is implemented by each templated pinToggler version
  virtual void doLoop() = 0;
  
};

// pinToggler is a singleton class, so can only be accessed via its static access method, `instance()`
template <int numPins>
class pinToggler : public pinTogglerBase {

private:
  inline int numDerivedPins() override { return numPins; }

  // Pin assignments
  uint8_t _LED_Pins[numPins] = {0};

  // Counters for each LED: these count up to 8
  volatile uint8_t _LED_count[numPins] = {0};

  // Increment values for each LED: these define how much to increment the counters
  // on each loop cycle
  volatile uint8_t _LED_add[numPins] = {0};

  // N.B. the constuctor is private so only access is via instance()
  pinToggler() {}

  // This method takes ownership of the base class's pointer, pointing
  // it at this templated instance
  void setupSingleton(const uint8_t LED_Pins[numPins]) {

    // Store the pin numbers and set as outputs, low
    for (size_t i = 0; i < numPins; i++) {
      
      _LED_Pins[i] = LED_Pins[i];
      
      pinMode(LED_Pins[i], OUTPUT );
      digitalWrite(LED_Pins[i], LOW);
    }

    // Setup the timer
    
    // Wipe any existing config for TIMER1
    noInterrupts();
    TCCR1A = 0;
    TCCR1B = 0;
    
    // Start the timer at the calculated value to allow for 1/FLASH_FREQ_HZ s
    // until trigger
    TCNT1 = timer1_counter;
    
    // Set the prescaler to 1024
    TCCR1B |= (1 << CS12);
    TCCR1B |= (1 << CS10);
    
    // Enable the TIMER1 overflow interrupt
    TIMSK1 |= (1 << TOIE1);

    // Reenable global interrupts
    interrupts();    
  }

  // Change the flash rate of an LED managed by this routine
  // This is called internally after setFlashRate looks up the global instance
  inline void setObjFlashRate(const size_t LED, const FLASHRATE rate) {
    _LED_add[LED] = rate;
  }
  
  // Do the loop. 
  // This gets called from the interrupt and toggles the appropriate pins
  void doLoop() override {
    
    // Loop over LEDs
    for (int i = 0; i < numPins; i++) {
      // Add to the counter val
      _LED_count[i] += _LED_add[i];

      // If it's greater than 8, reset and toggle
      if (_LED_count[i] >= 8) {
        _LED_count[i] = 0;
        digitalWrite(_LED_Pins[i], digitalRead(_LED_Pins[i]) ^ 1);
      }
    }
  }
  
public:

  // Initiate the pins and TIMER1
  static int init(const uint8_t * LED_Pins) {
    
    if (NULL != _instance)
      return -1;

    // Create the new object
    pinToggler * singletonInstance = new pinToggler;

    // Setup its pins
    singletonInstance->setupSingleton(LED_Pins);

    // Store it in the base class
    _instance = singletonInstance;    
  }

  // Change the flash rate of an LED managed by this routine
  // N.B. The LED number corresponds to ordering in the array passed to init()
  static int setFlashRate(const size_t LED, const FLASHRATE rate) {
    // Confirm that a) there is a static instance
    if (!_instance)
      return -1;

    // b) it's of the same template type as this method call (i.e. same num pins)
    if (_instance->numDerivedPins() != numPins)
      return -2;

    // and c) the user has requested a valid pin
    if (LED >= numPins)
      return -3;
      
    // Cast to the derived type
    pinToggler * ref = (pinToggler*)_instance;

    // Change the rate
    ref->_LED_add[LED] = rate;

    return 0;
  }
  
};

// Define the static instance
pinTogglerBase * pinTogglerBase::_instance = NULL;

// Interrupt service routine
// This is called by TIMER1 at a rate of FLASH_FREQ_HZ
ISR(TIMER1_OVF_vect)
{
  // Reset timer
  TCNT1 = pinTogglerBase::timer1_counter;

  // Call the derived loop function
  pinTogglerBase::instance()->doLoop();
}

