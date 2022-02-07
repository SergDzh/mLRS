//*******************************************************
// Copyright (c) MLRS project
// GPL3
// https://www.gnu.org/licenses/gpl-3.0.de.html
// OlliW @ www.olliw.eu
//*******************************************************
// rx hal splicer
//*******************************************************

// enter define into "MCU G++ Compiler"->"Preprocessor" !!!


#ifdef RX_SIYI_F373CC
#include "rx-hal-siyi-f373cc.h"
#endif

#ifdef TX_SIYI_F103C8
#include "tx-hal-siyi-f103c8.h"
#endif


#ifdef RX_DIY_BOARD01_F103CB
#include "rx-hal-diy-board01-f103cb.h"
#endif
#ifdef RX_DIY_E28_BOARD01_F103CB
#include "rx-hal-diy-e28-board01-f103cb.h"
#endif


#ifdef TX_DIY_E28_BOARD01_F103CB
#include "tx-hal-diy-e28-board01-f103cb.h"
#endif
#ifdef TX_DIY_MODULE01_G491RE
#include "tx-hal-diy-module01-g491re.h"
#endif


//-------------------------------------------------------
// Derived Defines
//-------------------------------------------------------
// should go somewhere else !?

#ifdef DEVICE_IS_TRANSMITTER

#ifdef DEVICE_HAS_DIVERSITY
  #if SETUP_TX_DIVERSITY == 1
    #define USE_ANTENNA1
  #elif SETUP_TX_DIVERSITY == 2
    #define USE_ANTENNA2
  #else
    #define USE_DIVERSITY
    #define USE_ANTENNA1
    #define USE_ANTENNA2
  #endif
#else
  #define USE_ANTENNA1
#endif

#endif
#ifdef DEVICE_IS_RECEIVER

#ifdef DEVICE_HAS_DIVERSITY
  #if SETUP_RX_DIVERSITY == 1
    #define USE_ANTENNA1
  #elif SETUP_RX_DIVERSITY == 2
    #define USE_ANTENNA2
  #else
    #define USE_DIVERSITY
    #define USE_ANTENNA1
    #define USE_ANTENNA2
  #endif
#else
  #define USE_ANTENNA1
#endif

#endif

