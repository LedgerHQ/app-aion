/*******************************************************************************
*   Ledger Blue
*   (c) 2016 Ledger
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/

#include "os.h"
#include "cx.h"
#include "stdbool.h"
#include "ethUstream.h"
#include "ethUtils.h"
#include "uint256.h"
#include "tokens.h"
#include "chainConfig.h"

#include "os_io_seproxyhal.h"
#include "string.h"

#include "glyphs.h"
#include "stdlib.h"

#define __NAME3(a, b, c) a##b##c
#define NAME3(a, b, c) __NAME3(a, b, c)

unsigned char G_io_seproxyhal_spi_buffer[IO_SEPROXYHAL_BUFFER_SIZE_B];


unsigned int io_seproxyhal_touch_settings(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_exit(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_tx_ok(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_tx_cancel(const bagl_element_t *e);
void ui_idle(void);

uint32_t set_result_get_publicKey(cx_ecfp_public_key_t* pubKey);

#define MAX_BIP32_PATH 10
#define KEY_SIZE 32
#define HASH_SIZE 32

#define CLA 0xE0
#define INS_GET_PUBLIC_KEY 0x02
#define INS_SIGN 0x04
#define INS_GET_APP_CONFIGURATION 0x06
#define INS_SIGN_PERSONAL_MESSAGE 0x08
#define P1_CONFIRM 0x01
#define P1_NON_CONFIRM 0x00
#define P2_NO_CHAINCODE 0x00
#define P2_CHAINCODE 0x01
#define P1_FIRST 0x00
#define P1_MORE 0x80

#define OFFSET_CLA 0
#define OFFSET_INS 1
#define OFFSET_P1 2
#define OFFSET_P2 3
#define OFFSET_LC 4
#define OFFSET_CDATA 5

#define nAmp_TO_AION 18
#define MAX_TRANSACTION_SIZE 230

unsigned char transaction_buffer[MAX_TRANSACTION_SIZE];
uint16_t buffer_counter = 0;
cx_blake2b_t blake;

static const uint8_t const TOKEN_TRANSFER_ID[] = { 0xa9, 0x05, 0x9c, 0xbb };
typedef struct tokenContext_t {
    uint8_t data[4 + 32 + 32];
    uint32_t dataFieldPos;
    bool provisioned;
} tokenContext_t;

typedef struct publicKeyContext_t {
    cx_ecfp_public_key_t publicKey;
    uint8_t address[65];
    uint8_t chainCode[32];
    bool getChaincode;
} publicKeyContext_t;

typedef struct transactionContext_t {
    uint8_t pathLength;
    uint32_t bip32Path[MAX_BIP32_PATH];
    uint8_t hash[32];
} transactionContext_t;

union {
    publicKeyContext_t publicKeyContext;
    transactionContext_t transactionContext;
} tmpCtx;
txContext_t txContext;

union {
  txContent_t txContent;
} tmpContent;

tokenContext_t tokenContext;
volatile uint8_t dataAllowed;
volatile char addressSummary[32];
volatile char fullAddress[67];
volatile char fullAmount[50];
volatile char maxFee[60];
volatile bool dataPresent;
volatile bool skipWarning;

bagl_element_t tmp_element;

#ifdef TARGET_NANOX
#include "ux.h"
ux_state_t G_ux;
bolos_ux_params_t G_ux_params;
#else // TARGET_NANOX
ux_state_t ux;

// display stepped screens
unsigned int ux_step;
unsigned int ux_step_count;
#endif // TARGET_NANOX


typedef struct internalStorage_t {
  unsigned char dataAllowed;
  uint8_t initialized;
} internalStorage_t;

WIDE internalStorage_t const N_storage_real;
#define N_storage (*(WIDE internalStorage_t*) PIC(&N_storage_real)) 

static const char const CONTRACT_ADDRESS[] = "New contract";

const unsigned char hex_digits[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

chain_config_t *chainConfig;                                    

void array_hexstr(char *strbuf, const void *bin, unsigned int len) {
    while (len--) {
        *strbuf++ = hex_digits[((*((char *)bin)) >> 4) & 0xF];
        *strbuf++ = hex_digits[(*((char *)bin)) & 0xF];
        bin = (const void *)((unsigned int)bin + 1);
    }
    *strbuf = 0; // EOS
}


const bagl_element_t* ui_menu_item_out_over(const bagl_element_t* e) {
  // the selection rectangle is after the none|touchable
  e = (const bagl_element_t*)(((unsigned int)e)+sizeof(bagl_element_t));
  return e;
}


#define BAGL_FONT_OPEN_SANS_LIGHT_16_22PX_AVG_WIDTH 10
#define BAGL_FONT_OPEN_SANS_REGULAR_10_13PX_AVG_WIDTH 8
#define MAX_CHAR_PER_LINE 25

#define COLOR_BG_1 0xF9F9F9
#define COLOR_APP 0x0ebdcf
#define COLOR_APP_LIGHT 0x87dee6

#if defined(TARGET_BLUE)

unsigned int map_color(unsigned int color) {
    switch(color) {
        case COLOR_APP:
            return chainConfig->color_header;

        case COLOR_APP_LIGHT:
            return chainConfig->color_dashboard;
    }
    return color;
}
void copy_element_and_map_coin_colors(const bagl_element_t* element) {
    os_memmove(&tmp_element, element, sizeof(bagl_element_t));
    tmp_element.component.fgcolor = map_color(tmp_element.component.fgcolor);
    tmp_element.component.bgcolor = map_color(tmp_element.component.bgcolor);
    tmp_element.overfgcolor = map_color(tmp_element.overfgcolor);
    tmp_element.overbgcolor = map_color(tmp_element.overbgcolor);
}

const bagl_element_t *ui_idle_blue_prepro(const bagl_element_t *element) {
    copy_element_and_map_coin_colors(element);
    if (element->component.userid == 0x01) {
        tmp_element.text = chainConfig->header_text;
    }
    return &tmp_element;
}

const bagl_element_t ui_idle_blue[] = {
  // type                               userid    x    y   w    h  str rad fill      fg        bg      fid iid  txt   touchparams...       ]
  {{BAGL_RECTANGLE                      , 0x00,   0,  68, 320, 413, 0, 0, BAGL_FILL, COLOR_BG_1, 0x000000, 0                                                                                 , 0   }, NULL, 0, 0, 0, NULL, NULL, NULL },

  // erase screen (only under the status bar)
  {{BAGL_RECTANGLE                      , 0x00,   0,  20, 320,  48, 0, 0, BAGL_FILL, COLOR_APP, COLOR_APP, 0                                                      , 0   }, NULL, 0, 0, 0, NULL, NULL, NULL},

  /// TOP STATUS BAR
  {{BAGL_LABELINE                       , 0x01,   0,  45, 320,  30, 0, 0, BAGL_FILL, 0xFFFFFF, COLOR_APP, BAGL_FONT_OPEN_SANS_SEMIBOLD_10_13PX|BAGL_FONT_ALIGNMENT_CENTER, 0   }, CHAINID_UPCASE, 0, 0, 0, NULL, NULL, NULL},

  {{BAGL_RECTANGLE | BAGL_FLAG_TOUCHABLE, 0x00,   0,  19,  56,  44, 0, 0, BAGL_FILL, COLOR_APP, COLOR_APP_LIGHT, BAGL_FONT_SYMBOLS_0|BAGL_FONT_ALIGNMENT_CENTER|BAGL_FONT_ALIGNMENT_MIDDLE, 0 }, BAGL_FONT_SYMBOLS_0_SETTINGS, 0, COLOR_APP, 0xFFFFFF, io_seproxyhal_touch_settings, NULL, NULL},
  {{BAGL_RECTANGLE | BAGL_FLAG_TOUCHABLE, 0x00, 264,  19,  56,  44, 0, 0, BAGL_FILL, COLOR_APP, COLOR_APP_LIGHT, BAGL_FONT_SYMBOLS_0|BAGL_FONT_ALIGNMENT_CENTER|BAGL_FONT_ALIGNMENT_MIDDLE, 0 }, BAGL_FONT_SYMBOLS_0_DASHBOARD, 0, COLOR_APP, 0xFFFFFF, io_seproxyhal_touch_exit, NULL, NULL},

  // BADGE_<CHAINID>.GIF
  //{{BAGL_ICON                           , 0x00, 135, 178,  50,  50, 0, 0, BAGL_FILL, 0       , COLOR_BG_1, 0                                                              ,0   } , &NAME3(C_blue_badge_, CHAINID, ), 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_LABELINE                       , 0x00,   0, 270, 320,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_LIGHT_16_22PX|BAGL_FONT_ALIGNMENT_CENTER, 0   }, "Open your wallet", 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x00,   0, 308, 320,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX|BAGL_FONT_ALIGNMENT_CENTER, 0   }, "Connect your Ledger Blue and open your", 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x00,   0, 331, 320,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX|BAGL_FONT_ALIGNMENT_CENTER, 0   }, "preferred wallet to view your accounts.", 0, 0, 0, NULL, NULL, NULL},

  {{BAGL_LABELINE                       , 0x00,   0, 450, 320,  14, 0, 0, 0        , 0x999999, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_8_11PX|BAGL_FONT_ALIGNMENT_CENTER, 0   }, "Validation requests will show automatically.", 10, 0, COLOR_BG_1, NULL, NULL, NULL },
};

unsigned int ui_idle_blue_button(unsigned int button_mask, unsigned int button_mask_counter) {
  return 0;
}
#endif // #if defined(TARGET_BLUE)


#if defined(TARGET_NANOS)


const ux_menu_entry_t menu_main[];
const ux_menu_entry_t menu_settings[];
//const ux_menu_entry_t menu_settings_browser[];
const ux_menu_entry_t menu_settings_data[];

#ifdef HAVE_U2F

// change the setting
void menu_settings_data_change(unsigned int enabled) {
  dataAllowed = enabled;
  nvm_write(&N_storage.dataAllowed, (void*)&dataAllowed, sizeof(uint8_t));
  // go back to the menu entry
  UX_MENU_DISPLAY(0, menu_settings, NULL);
}

// show the currently activated entry
void menu_settings_data_init(unsigned int ignored) {
  UNUSED(ignored);
  UX_MENU_DISPLAY(N_storage.dataAllowed?1:0, menu_settings_data, NULL);
}

const ux_menu_entry_t menu_settings_data[] = {
  {NULL, menu_settings_data_change, 0, NULL, "No", NULL, 0, 0},
  {NULL, menu_settings_data_change, 1, NULL, "Yes", NULL, 0, 0},
  UX_MENU_END
};

const ux_menu_entry_t menu_settings[] = {
  {NULL, menu_settings_data_init, 0, NULL, "Contract data", NULL, 0, 0},
  {menu_main, NULL, 1, &C_icon_back, "Back", NULL, 61, 40},
  UX_MENU_END
};
#endif // HAVE_U2F

const ux_menu_entry_t menu_about[] = {
  {NULL, NULL, 0, NULL, "Version", APPVERSION , 0, 0},
  {menu_main, NULL, 2, &C_icon_back, "Back", NULL, 61, 40},
  UX_MENU_END
};

const ux_menu_entry_t menu_main[] = {
  //{NULL, NULL, 0, &NAME3(C_nanos_badge_, CHAINID, ), "Use wallet to", "view accounts", 33, 12},
  {NULL, NULL, 0, NULL, "Use wallet to", "view accounts", 0, 0},
  {menu_settings, NULL, 0, NULL, "Settings", NULL, 0, 0},
  {menu_about, NULL, 0, NULL, "About", NULL, 0, 0},
  {NULL, os_sched_exit, 0, &C_icon_dashboard, "Quit app", NULL, 50, 29},
  UX_MENU_END
};

#endif // #if defined(TARGET_NANOS)

#if defined(TARGET_BLUE)
const bagl_element_t * ui_settings_blue_toggle_data(const bagl_element_t * e) {
  // swap setting and request redraw of settings elements
  uint8_t setting = N_storage.dataAllowed?0:1;
  nvm_write(&N_storage.dataAllowed, (void*)&setting, sizeof(uint8_t));

  // only refresh settings mutable drawn elements
  UX_REDISPLAY_IDX(7);

  // won't redisplay the bagl_none
  return 0;
}

// don't perform any draw/color change upon finger event over settings
const bagl_element_t* ui_settings_out_over(const bagl_element_t* e) {
  return NULL;
}

unsigned int ui_settings_back_callback(const bagl_element_t* e) {
  // go back to idle
  ui_idle();
  return 0;
}

const bagl_element_t ui_settings_blue[] = {
  // type                               userid    x    y   w    h  str rad fill      fg        bg      fid iid  txt   touchparams...       ]
  {{BAGL_RECTANGLE                      , 0x00,   0,  68, 320, 413, 0, 0, BAGL_FILL, COLOR_BG_1, 0x000000, 0                                                                                 , 0   }, NULL, 0, 0, 0, NULL, NULL, NULL },

  // erase screen (only under the status bar)
  {{BAGL_RECTANGLE                      , 0x00,   0,  20, 320,  48, 0, 0, BAGL_FILL, COLOR_APP, COLOR_APP, 0                                                      , 0   }, NULL, 0, 0, 0, NULL, NULL, NULL},

  /// TOP STATUS BAR
  {{BAGL_LABELINE                       , 0x00,   0,  45, 320,  30, 0, 0, BAGL_FILL, 0xFFFFFF, COLOR_APP, BAGL_FONT_OPEN_SANS_SEMIBOLD_10_13PX|BAGL_FONT_ALIGNMENT_CENTER, 0   }, "SETTINGS", 0, 0, 0, NULL, NULL, NULL},

  {{BAGL_RECTANGLE | BAGL_FLAG_TOUCHABLE, 0x00,   0,  19,  50,  44, 0, 0, BAGL_FILL, COLOR_APP, COLOR_APP_LIGHT, BAGL_FONT_SYMBOLS_0|BAGL_FONT_ALIGNMENT_CENTER|BAGL_FONT_ALIGNMENT_MIDDLE, 0 }, BAGL_FONT_SYMBOLS_0_LEFT, 0, COLOR_APP, 0xFFFFFF, ui_settings_back_callback, NULL, NULL},
  //{{BAGL_RECTANGLE | BAGL_FLAG_TOUCHABLE, 0x00, 264,  19,  56,  44, 0, 0, BAGL_FILL, COLOR_APP, COLOR_APP_LIGHT, BAGL_FONT_SYMBOLS_0|BAGL_FONT_ALIGNMENT_CENTER|BAGL_FONT_ALIGNMENT_MIDDLE, 0 }, BAGL_FONT_SYMBOLS_0_DASHBOARD, 0, COLOR_APP, 0xFFFFFF, io_seproxyhal_touch_exit, NULL, NULL},


  {{BAGL_LABELINE                       , 0x00,  30, 105, 160,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, "Contract data", 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x00,  30, 126, 260,  30, 0, 0, BAGL_FILL, 0x999999, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_8_11PX, 0   }, "Allow contract data in transactions", 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_NONE   | BAGL_FLAG_TOUCHABLE   , 0x00,   0,  78, 320,  68, 0, 0, BAGL_FILL, 0xFFFFFF, 0x000000, 0                                                                                        , 0   }, NULL, 0, 0xEEEEEE, 0x000000, ui_settings_blue_toggle_data, ui_settings_out_over, ui_settings_out_over },

  {{BAGL_ICON                           , 0x01, 258,  98,  32,  18, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, 0, 0   }, NULL, 0, 0, 0, NULL, NULL, NULL},
};

const bagl_element_t * ui_settings_blue_prepro(const bagl_element_t * e) {
  copy_element_and_map_coin_colors(e);
  // none elements are skipped
  if ((e->component.type&(~BAGL_FLAG_TOUCHABLE)) == BAGL_NONE) {
    return 0;
  }
  // swap icon buffer to be displayed depending on if corresponding setting is enabled or not.
  if (e->component.userid) {
    switch(e->component.userid) {
      case 0x01:
        // swap icon content
        if (N_storage.dataAllowed) {
          tmp_element.text = &C_icon_toggle_set;
        }
        else {
          tmp_element.text = &C_icon_toggle_reset;
        }
        break;
    }
  }
  return &tmp_element;
}

unsigned int ui_settings_blue_button(unsigned int button_mask, unsigned int button_mask_counter) {
  return 0;  
}
#endif // #if defined(TARGET_BLUE)




#if defined(TARGET_BLUE)
// reuse addressSummary for each line content
const char* ui_details_title;
const char* ui_details_content;
typedef void (*callback_t)(void);
callback_t ui_details_back_callback;

const bagl_element_t* ui_details_blue_back_callback(const bagl_element_t* element) {
  ui_details_back_callback();
  return 0;
}


const bagl_element_t ui_details_blue[] = {
  // erase screen (only under the status bar)
  {{BAGL_RECTANGLE                      , 0x00,   0,  68, 320, 413, 0, 0, BAGL_FILL, COLOR_BG_1, 0x000000, 0                                                                                 , 0   }, NULL, 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_RECTANGLE                      , 0x00,   0,  20, 320,  48, 0, 0, BAGL_FILL, COLOR_APP, COLOR_APP, 0                                                      , 0   }, NULL, 0, 0, 0, NULL, NULL, NULL},

  /// TOP STATUS BAR
  {{BAGL_LABELINE                       , 0x01,   0,  45, 320,  30, 0, 0, BAGL_FILL, 0xFFFFFF, COLOR_APP, BAGL_FONT_OPEN_SANS_SEMIBOLD_10_13PX|BAGL_FONT_ALIGNMENT_CENTER, 0   }, addressSummary, 0, 0, 0, NULL, NULL, NULL},

  {{BAGL_RECTANGLE | BAGL_FLAG_TOUCHABLE, 0x00,   0,  19,  50,  44, 0, 0, BAGL_FILL, COLOR_APP, COLOR_APP_LIGHT, BAGL_FONT_SYMBOLS_0|BAGL_FONT_ALIGNMENT_CENTER|BAGL_FONT_ALIGNMENT_MIDDLE, 0 }, BAGL_FONT_SYMBOLS_0_LEFT, 0, COLOR_APP, 0xFFFFFF, ui_details_blue_back_callback, NULL, NULL},
  //{{BAGL_RECTANGLE | BAGL_FLAG_TOUCHABLE, 0x00, 264,  19,  56,  44, 0, 0, BAGL_FILL, COLOR_APP, COLOR_APP_LIGHT, BAGL_FONT_SYMBOLS_0|BAGL_FONT_ALIGNMENT_CENTER|BAGL_FONT_ALIGNMENT_MIDDLE, 0 }, " " /*BAGL_FONT_SYMBOLS_0_DASHBOARD*/, 0, COLOR_APP, 0xFFFFFF, io_seproxyhal_touch_exit, NULL, NULL},

  {{BAGL_LABELINE                       , 0x00,  30, 106, 320,  30, 0, 0, BAGL_FILL, 0x999999, COLOR_BG_1, BAGL_FONT_OPEN_SANS_SEMIBOLD_8_11PX, 0   }, "VALUE", 0, 0, 0, NULL, NULL, NULL},

  {{BAGL_LABELINE                       , 0x10,  30, 136, 260,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, addressSummary, 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x11,  30, 159, 260,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, addressSummary, 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x12,  30, 182, 260,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, addressSummary, 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x13,  30, 205, 260,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, addressSummary, 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x14,  30, 228, 260,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, addressSummary, 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x15,  30, 251, 260,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, addressSummary, 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x16,  30, 274, 260,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, addressSummary, 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x17,  30, 297, 260,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, addressSummary, 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x18,  30, 320, 260,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, addressSummary, 0, 0, 0, NULL, NULL, NULL},
  //"..." at the end if too much
  {{BAGL_LABELINE                       , 0x19,  30, 343, 260,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, addressSummary, 0, 0, 0, NULL, NULL, NULL},

  {{BAGL_LABELINE                       , 0x00,   0, 450, 320,  14, 0, 0, 0        , 0x999999, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_8_11PX|BAGL_FONT_ALIGNMENT_CENTER, 0   }, "Review the whole value before continuing.", 10, 0, COLOR_BG_1, NULL, NULL, NULL },
};

const bagl_element_t* ui_details_blue_prepro(const bagl_element_t* element) {
  copy_element_and_map_coin_colors(element);
  if (element->component.userid == 1) {
    tmp_element.text = ui_details_title;
    return &tmp_element;
  }
  else if(element->component.userid > 0) {
    unsigned int length = strlen(ui_details_content);
    if (length >= (element->component.userid & 0xF) * MAX_CHAR_PER_LINE) {
      os_memset(addressSummary, 0, MAX_CHAR_PER_LINE+1);
      os_memmove(addressSummary, ui_details_content+(element->component.userid & 0xF) * MAX_CHAR_PER_LINE, MIN(length - (element->component.userid & 0xF) * MAX_CHAR_PER_LINE, MAX_CHAR_PER_LINE));
      return &tmp_element;
    }
    // nothing to draw for this line
    return 0;
  }
  return &tmp_element;
}

unsigned int ui_details_blue_button(unsigned int button_mask, unsigned int button_mask_counter) {
  return 0;
}

void ui_details_init(const char* title, const char* content, callback_t back_callback) {
  ui_details_title = title;
  ui_details_content = content;
  ui_details_back_callback = back_callback;
  UX_DISPLAY(ui_details_blue, ui_details_blue_prepro);
}

void ui_approval_blue_init(void);

bagl_element_callback_t ui_approval_blue_ok;
bagl_element_callback_t ui_approval_blue_cancel;

const bagl_element_t* ui_approval_blue_ok_callback(const bagl_element_t* e) {
  return ui_approval_blue_ok(e);
}

const bagl_element_t* ui_approval_blue_cancel_callback(const bagl_element_t* e) {
  return ui_approval_blue_cancel(e);
}

typedef enum {
  APPROVAL_TRANSACTION,    
  APPROVAL_MESSAGE, 
} ui_approval_blue_state_t;
ui_approval_blue_state_t G_ui_approval_blue_state;
// pointer to value to be displayed
const char* ui_approval_blue_values[3];
// variable part of the structure
const char* const ui_approval_blue_details_name[][5] = {
  /*APPROVAL_TRANSACTION*/    
  {"AMOUNT",  "ADDRESS", "MAX FEES","CONFIRM TRANSACTION","Transaction details",},
  
  /*APPROVAL_MESSAGE*/ 
  {"HASH",    NULL,      NULL,      "SIGN MESSAGE",       "Message signature", },  
};

const bagl_element_t* ui_approval_blue_1_details(const bagl_element_t* e) {
  if (strlen(ui_approval_blue_values[0])*BAGL_FONT_OPEN_SANS_LIGHT_16_22PX_AVG_WIDTH >= 160) {
    // display details screen
    ui_details_init(ui_approval_blue_details_name[G_ui_approval_blue_state][0], ui_approval_blue_values[0], ui_approval_blue_init);
  }
  return 0;
};

const bagl_element_t* ui_approval_blue_2_details(const bagl_element_t* e) {
  if (strlen(ui_approval_blue_values[1])*BAGL_FONT_OPEN_SANS_REGULAR_10_13PX_AVG_WIDTH >= 160) {
    ui_details_init(ui_approval_blue_details_name[G_ui_approval_blue_state][1], ui_approval_blue_values[1], ui_approval_blue_init);
  }
  return 0;
};

const bagl_element_t* ui_approval_blue_3_details(const bagl_element_t* e) {
  if (strlen(ui_approval_blue_values[2])*BAGL_FONT_OPEN_SANS_REGULAR_10_13PX_AVG_WIDTH >= 160) {
    ui_details_init(ui_approval_blue_details_name[G_ui_approval_blue_state][2], ui_approval_blue_values[2], ui_approval_blue_init);
  }
  return 0;
};

const bagl_element_t ui_approval_blue[] = {
  {{BAGL_RECTANGLE                      , 0x00,   0,  68, 320, 413, 0, 0, BAGL_FILL, COLOR_BG_1, 0x000000, 0                                                                                 , 0   }, NULL, 0, 0, 0, NULL, NULL, NULL },

  // erase screen (only under the status bar)
  {{BAGL_RECTANGLE                      , 0x00,   0,  20, 320,  48, 0, 0, BAGL_FILL, COLOR_APP, COLOR_APP, 0                                                      , 0   }, NULL, 0, 0, 0, NULL, NULL, NULL},

  /// TOP STATUS BAR
  {{BAGL_LABELINE                       , 0x60,   0,  45, 320,  30, 0, 0, BAGL_FILL, 0xFFFFFF, COLOR_APP, BAGL_FONT_OPEN_SANS_SEMIBOLD_10_13PX|BAGL_FONT_ALIGNMENT_CENTER, 0   }, NULL, 0, 0, 0, NULL, NULL, NULL},

  // BADGE_TRANSACTION.GIF
  {{BAGL_ICON                           , 0x40,  30,  98,  50,  50, 0, 0, BAGL_FILL, 0       , COLOR_BG_1, 0                                                                                 , 0  } , &C_badge_transaction, 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_LABELINE                       , 0x50, 100, 117, 320,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX, 0   }, NULL, 0, 0, 0, NULL, NULL, NULL},

  {{BAGL_LABELINE                       , 0x00, 100, 138, 320,  30, 0, 0, BAGL_FILL, 0x999999, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_8_11PX, 0   }, "Check and confirm values", 0, 0, 0, NULL, NULL, NULL},


  {{BAGL_LABELINE                       , 0x70,  30, 196, 100,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_SEMIBOLD_8_11PX, 0   }, NULL, 0, 0, 0, NULL, NULL, NULL}, // AMOUNT
  // x-18 when ...
  {{BAGL_LABELINE                       , 0x10, 130, 200, 160,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_LIGHT_16_22PX|BAGL_FONT_ALIGNMENT_RIGHT, 0   }, NULL, 0, 0, 0, NULL, NULL, NULL}, // fullAmount
  {{BAGL_LABELINE                       , 0x20, 284, 196,   6,  16, 0, 0, BAGL_FILL, 0x999999, COLOR_BG_1, BAGL_FONT_SYMBOLS_0|BAGL_FONT_ALIGNMENT_RIGHT, 0   }, BAGL_FONT_SYMBOLS_0_MINIRIGHT, 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_NONE   | BAGL_FLAG_TOUCHABLE   , 0x00,   0, 168, 320,  48, 0, 9, BAGL_FILL, 0xFFFFFF, 0x000000, 0                                                                                        , 0   }, NULL, 0, 0xEEEEEE, 0x000000, ui_approval_blue_1_details, ui_menu_item_out_over, ui_menu_item_out_over },
  {{BAGL_RECTANGLE                      , 0x20,   0, 168,   5,  48, 0, 0, BAGL_FILL, COLOR_BG_1, COLOR_BG_1, 0                                                                                    , 0   }, NULL, 0, 0x41CCB4, 0, NULL, NULL, NULL },
  
  {{BAGL_RECTANGLE                      , 0x31,  30, 216, 260,   1, 1, 0, 0        , 0xEEEEEE, COLOR_BG_1, 0                                                                                    , 0   }, NULL, 0, 0, 0, NULL, NULL, NULL },


  {{BAGL_LABELINE                       , 0x71,  30, 245, 100,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_SEMIBOLD_8_11PX, 0   }, NULL, 0, 0, 0, NULL, NULL, NULL}, // ADDRESS
  // x-18 when ...
  {{BAGL_LABELINE                       , 0x11, 130, 245, 160,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX|BAGL_FONT_ALIGNMENT_RIGHT, 0   }, NULL, 0, 0, 0, NULL, NULL, NULL}, // fullAddress
  {{BAGL_LABELINE                       , 0x21, 284, 245,   6,  16, 0, 0, BAGL_FILL, 0x999999, COLOR_BG_1, BAGL_FONT_SYMBOLS_0|BAGL_FONT_ALIGNMENT_RIGHT, 0   }, BAGL_FONT_SYMBOLS_0_MINIRIGHT, 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_NONE   | BAGL_FLAG_TOUCHABLE   , 0x00,   0, 217, 320,  48, 0, 9, BAGL_FILL, 0xFFFFFF, 0x000000, 0                                                                                        , 0   }, NULL, 0, 0xEEEEEE, 0x000000, ui_approval_blue_2_details, ui_menu_item_out_over, ui_menu_item_out_over },
  {{BAGL_RECTANGLE                      , 0x21,   0, 217,   5,  48, 0, 0, BAGL_FILL, COLOR_BG_1, COLOR_BG_1, 0                                                                                    , 0   }, NULL, 0, 0x41CCB4, 0, NULL, NULL, NULL },  

  {{BAGL_RECTANGLE                      , 0x32,  30, 265, 260,   1, 1, 0, 0        , 0xEEEEEE, COLOR_BG_1, 0                                                                                    , 0   }, NULL, 0, 0, 0, NULL, NULL, NULL },
  

  {{BAGL_LABELINE                       , 0x72,  30, 294, 100,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_SEMIBOLD_8_11PX, 0   }, NULL, 0, 0, 0, NULL, NULL, NULL}, // MAX FEES
  // x-18 when ...
  {{BAGL_LABELINE                       , 0x12, 130, 294, 160,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX|BAGL_FONT_ALIGNMENT_RIGHT, 0   }, NULL, 0, 0, 0, NULL, NULL, NULL}, //maxFee
  {{BAGL_LABELINE                       , 0x22, 284, 294,   6,  16, 0, 0, BAGL_FILL, 0x999999, COLOR_BG_1, BAGL_FONT_SYMBOLS_0|BAGL_FONT_ALIGNMENT_RIGHT, 0   }, BAGL_FONT_SYMBOLS_0_MINIRIGHT, 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_NONE   | BAGL_FLAG_TOUCHABLE   , 0x00,   0, 266, 320,  48, 0, 9, BAGL_FILL, 0xFFFFFF, 0x000000, 0                                                                                        , 0   }, NULL, 0, 0xEEEEEE, 0x000000, ui_approval_blue_3_details, ui_menu_item_out_over, ui_menu_item_out_over },
  {{BAGL_RECTANGLE                      , 0x22,   0, 266,   5,  48, 0, 0, BAGL_FILL, COLOR_BG_1, COLOR_BG_1, 0                                                                                    , 0   }, NULL, 0, 0x41CCB4, 0, NULL, NULL, NULL },


  {{BAGL_RECTANGLE                      , 0x06,  30, 314, 260,   1, 1, 0, 0        , 0xEEEEEE, COLOR_BG_1, 0                                                                                    , 0   }, NULL, 0, 0, 0, NULL, NULL, NULL },
  

  {{BAGL_LABELINE                       , 0x06,  30, 343, 120,  30, 0, 0, BAGL_FILL, 0x000000, COLOR_BG_1, BAGL_FONT_OPEN_SANS_SEMIBOLD_8_11PX, 0   }, "CONTRACT DATA", 0, 0, 0, NULL, NULL, NULL},

  //{{BAGL_LABELINE                       , 0x05, 130, 343, 160,  30, 0, 0, BAGL_FILL, 0x666666, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX|BAGL_FONT_ALIGNMENT_RIGHT, 0   }, "Not present", 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_LABELINE                       , 0x06, 133, 343, 140,  30, 0, 0, BAGL_FILL, 0x666666, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_10_13PX|BAGL_FONT_ALIGNMENT_RIGHT, 0   }, "Present", 0, 0, 0, NULL, NULL, NULL},
  {{BAGL_ICON                           , 0x06, 278, 333,  12,  12, 0, 0, BAGL_FILL, 0       , COLOR_BG_1, 0                                                                   , 0}, &C_icon_warning, 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_RECTANGLE | BAGL_FLAG_TOUCHABLE, 0x00,  40, 414, 115,  36, 0,18, BAGL_FILL, 0xCCCCCC, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_11_14PX|BAGL_FONT_ALIGNMENT_CENTER|BAGL_FONT_ALIGNMENT_MIDDLE, 0 }, "REJECT", 0, 0xB7B7B7, COLOR_BG_1, ui_approval_blue_cancel_callback, NULL, NULL},
  {{BAGL_RECTANGLE | BAGL_FLAG_TOUCHABLE, 0x00, 165, 414, 115,  36, 0,18, BAGL_FILL, 0x41ccb4, COLOR_BG_1, BAGL_FONT_OPEN_SANS_REGULAR_11_14PX|BAGL_FONT_ALIGNMENT_CENTER|BAGL_FONT_ALIGNMENT_MIDDLE, 0 }, "CONFIRM", 0, 0x3ab7a2, COLOR_BG_1, ui_approval_blue_ok_callback, NULL, NULL},

};

const bagl_element_t* ui_approval_blue_prepro(const bagl_element_t* element) {
    copy_element_and_map_coin_colors(element);
    if (element->component.userid == 0) {
      return &tmp_element;
    }
    // none elements are skipped
    if ((element->component.type&(~BAGL_FLAG_TOUCHABLE)) == BAGL_NONE) {
      return 0;
    }
    else {
      switch(element->component.userid&0xF0) {

        // icon
        case 0x40:
          return &tmp_element; 
          break;
		  
        // TITLE
        case 0x60: 
          tmp_element.text = ui_approval_blue_details_name[G_ui_approval_blue_state][3];
          return &tmp_element;
          break;
		  
        // SUBLINE
        case 0x50: 
          tmp_element.text = ui_approval_blue_details_name[G_ui_approval_blue_state][4];
          return &tmp_element;

        // details label
        case 0x70:
          if (!ui_approval_blue_details_name[G_ui_approval_blue_state][element->component.userid&0xF]) {
            return NULL;
          }
          tmp_element.text = ui_approval_blue_details_name[G_ui_approval_blue_state][element->component.userid&0xF];
          return &tmp_element;

        // detail value
        case 0x10:
          // won't display
          if (!ui_approval_blue_details_name[G_ui_approval_blue_state][element->component.userid&0xF]) {
            return NULL;
          }
          // always display the value
          tmp_element.text = ui_approval_blue_values[(element->component.userid&0xF)];

          // x -= 18 when overflow is detected
          if (strlen(ui_approval_blue_values[(element->component.userid&0xF)])*BAGL_FONT_OPEN_SANS_LIGHT_16_22PX_AVG_WIDTH >= 160) {
            tmp_element.component.x -= 18;
          }
          return &tmp_element;
          break;

        // right arrow and left selection rectangle
        case 0x20:
          if (!ui_approval_blue_details_name[G_ui_approval_blue_state][element->component.userid&0xF]) {
            return NULL;
          }
          if (strlen(ui_approval_blue_values[(element->component.userid&0xF)])*BAGL_FONT_OPEN_SANS_LIGHT_16_22PX_AVG_WIDTH < 160) {
            return NULL;
          }

        // horizontal delimiter
        case 0x30:
          return ui_approval_blue_details_name[G_ui_approval_blue_state][element->component.userid&0xF]!=NULL?&tmp_element:NULL;


        case 0x05:
          return !dataPresent?&tmp_element:NULL; 
        case 0x06:
          return dataPresent?&tmp_element:NULL;
      }
    }
    return &tmp_element;
}
unsigned int ui_approval_blue_button(unsigned int button_mask, unsigned int button_mask_counter) {
  return 0;
}

#endif // #if defined(TARGET_BLUE)

 #if defined(TARGET_BLUE)

unsigned int ui_address_blue_prepro(const bagl_element_t* element) {
  copy_element_and_map_coin_colors(element);
  if(element->component.userid > 0) {
    unsigned int length = strlen(fullAddress);
    if (length >= (element->component.userid & 0xF) * MAX_CHAR_PER_LINE) {
      os_memset(addressSummary, 0, MAX_CHAR_PER_LINE+1);
      os_memmove(addressSummary, fullAddress+(element->component.userid & 0xF) * MAX_CHAR_PER_LINE, MIN(length - (element->component.userid & 0xF) * MAX_CHAR_PER_LINE, MAX_CHAR_PER_LINE));
      return &tmp_element;
    }
    // nothing to draw for this line
    return 0;
  }
  return &tmp_element;
}

unsigned int ui_address_blue_button(unsigned int button_mask, unsigned int button_mask_counter) {
  return 0;
}
#endif // #if defined(TARGET_BLUE)

#if defined(TARGET_NANOS)
const bagl_element_t ui_address_nanos[] = {
  // type                               userid    x    y   w    h  str rad fill      fg        bg      fid iid  txt   touchparams...       ]
  {{BAGL_RECTANGLE                      , 0x00,   0,   0, 128,  32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF, 0, 0}, NULL, 0, 0, 0, NULL, NULL, NULL},

  {{BAGL_ICON                           , 0x00,   3,  12,   7,   7, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_CROSS  }, NULL, 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_ICON                           , 0x00, 117,  13,   8,   6, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_CHECK  }, NULL, 0, 0, 0, NULL, NULL, NULL },

  //{{BAGL_ICON                           , 0x01,  31,   9,  14,  14, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_EYE_BADGE  }, NULL, 0, 0, 0, NULL, NULL, NULL },  
  {{BAGL_LABELINE                       , 0x01,   0,  12, 128,  12, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "Confirm", 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x01,   0,  26, 128,  12, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "address", 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_LABELINE                       , 0x02,   0,  12, 128,  12, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_REGULAR_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "Address", 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x02,  23,  26,  82,  12, 0x80|10, 0, 0  , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 26  }, (char*)fullAddress, 0, 0, 0, NULL, NULL, NULL },
};

unsigned int ui_address_prepro(const bagl_element_t* element) {
    if (element->component.userid > 0) {
        unsigned int display = (ux_step == element->component.userid-1);
        if(display) {
          switch(element->component.userid) {
          case 1:
            UX_CALLBACK_SET_INTERVAL(2000);
            break;
          case 2:
            UX_CALLBACK_SET_INTERVAL(MAX(3000, 1000+bagl_label_roundtrip_duration_ms(element, 7)));
            break;
          }
        }
        return display;
    }
    return 1;
}

unsigned int ui_address_nanos_button(unsigned int button_mask, unsigned int button_mask_counter);
#endif // #if defined(TARGET_NANOS)



#if defined(TARGET_NANOS)
const bagl_element_t ui_approval_nanos[] = {
  // type                               userid    x    y   w    h  str rad fill      fg        bg      fid iid  txt   touchparams...       ]
  {{BAGL_RECTANGLE                      , 0x00,   0,   0, 128,  32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF, 0, 0}, NULL, 0, 0, 0, NULL, NULL, NULL},

  {{BAGL_ICON                           , 0x00,   3,  12,   7,   7, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_CROSS  }, NULL, 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_ICON                           , 0x00, 117,  13,   8,   6, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_CHECK  }, NULL, 0, 0, 0, NULL, NULL, NULL },

  //{{BAGL_ICON                           , 0x01,  21,   9,  14,  14, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_TRANSACTION_BADGE  }, NULL, 0, 0, 0, NULL, NULL, NULL },  
  {{BAGL_LABELINE                       , 0x01,   0,  12, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "Confirm", 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x01,   0,  26, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "transaction", 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_LABELINE                       , 0x02,   0,  12, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_REGULAR_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "WARNING", 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x02,  23,  26,  82,  12, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 0   }, "Data present", 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_LABELINE                       , 0x03,   0,  12, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_REGULAR_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "Amount", 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x03,  23,  26,  82,  12, 0x80|10, 0, 0  , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 26  }, (char*)fullAmount, 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_LABELINE                       , 0x04,   0,  12, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_REGULAR_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "Address", 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x04,  23,  26,  82,  12, 0x80|10, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 50   }, (char*)fullAddress, 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_LABELINE                       , 0x05,   0,  12, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_REGULAR_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "Maximum fees", 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x05,  23,  26,  82,  12, 0x80|10, 0, 0  , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 26  }, (char*)maxFee, 0, 0, 0, NULL, NULL, NULL },

};

unsigned int ui_approval_prepro(const bagl_element_t* element) {
    unsigned int display = 1;
    if (element->component.userid > 0) {
        display = (ux_step == element->component.userid-1);
        if(display) {
          switch(element->component.userid) {
          case 1:
            UX_CALLBACK_SET_INTERVAL(2000);
            break;
          case 2:
            if (dataPresent) {
              UX_CALLBACK_SET_INTERVAL(3000);
            }
            else {
              display = 0;
              ux_step++; // display the next step
            }
            break;          
          case 3:
            UX_CALLBACK_SET_INTERVAL(MAX(3000, 1000+bagl_label_roundtrip_duration_ms(element, 7)));
            break;
          case 4:
            UX_CALLBACK_SET_INTERVAL(MAX(3000, 1000+bagl_label_roundtrip_duration_ms(element, 7)));
            break;
          case 5:
            UX_CALLBACK_SET_INTERVAL(MAX(3000, 1000+bagl_label_roundtrip_duration_ms(element, 7)));
            break;
          }
        }
    }
    return display;
}

unsigned int ui_approval_nanos_button(unsigned int button_mask, unsigned int button_mask_counter);

const bagl_element_t ui_approval_signMessage_nanos[] = {
  // type                               userid    x    y   w    h  str rad fill      fg        bg      fid iid  txt   touchparams...       ]
  {{BAGL_RECTANGLE                      , 0x00,   0,   0, 128,  32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF, 0, 0}, NULL, 0, 0, 0, NULL, NULL, NULL},
 
  {{BAGL_ICON                           , 0x00,   3,  12,   7,   7, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_CROSS  }, NULL, 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_ICON                           , 0x00, 117,  13,   8,   6, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_CHECK  }, NULL, 0, 0, 0, NULL, NULL, NULL },

  //{{BAGL_ICON                           , 0x01,  28,   9,  14,  14, 0, 0, 0        , 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_TRANSACTION_BADGE  }, NULL, 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x01,   0,  12, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "Sign the", 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x01,   0,  26, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "message", 0, 0, 0, NULL, NULL, NULL },

  {{BAGL_LABELINE                       , 0x02,   0,  12, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_REGULAR_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, "Message hash", 0, 0, 0, NULL, NULL, NULL },
  {{BAGL_LABELINE                       , 0x02,   0,  26, 128,  32, 0, 0, 0        , 0xFFFFFF, 0x000000, BAGL_FONT_OPEN_SANS_EXTRABOLD_11px|BAGL_FONT_ALIGNMENT_CENTER, 0  }, fullAddress, 0, 0, 0, NULL, NULL, NULL },

};

unsigned int
ui_approval_signMessage_nanos_button(unsigned int button_mask, unsigned int button_mask_counter);

unsigned int ui_approval_signMessage_prepro(const bagl_element_t *element) {
    if (element->component.userid > 0) {
        switch (element->component.userid) {
        case 1:
            UX_CALLBACK_SET_INTERVAL(2000);
            break;
        case 2:
            UX_CALLBACK_SET_INTERVAL(3000);
            break;
        }
        return (ux_step == element->component.userid - 1);
    }
    return 1;
}

#endif // #if defined(TARGET_NANOS)

#if defined(TARGET_NANOX)

void display_settings(void);
void switch_settings_contract_data(void);

//////////////////////////////////////////////////////////////////////
UX_STEP_NOCB(
    ux_idle_flow_1_step,
    pnn, //pnn,
    {
      &C_nanox_app_aion,
      "Application",
      "is ready",
    });
UX_STEP_VALID(
    ux_idle_flow_2_step,
    pb,
    display_settings(),
    {
      &C_icon_coggle,
      "Settings",
    });
UX_STEP_NOCB(
    ux_idle_flow_3_step,
    bn,
    {
      "Version",
      APPVERSION,
    });
UX_STEP_VALID(
    ux_idle_flow_4_step,
    pb,
    os_sched_exit(-1),
    {
      &C_icon_dashboard_x,
      "Quit",
    });
const ux_flow_step_t *        const ux_idle_flow [] = {
  &ux_idle_flow_1_step,
  &ux_idle_flow_2_step,
  &ux_idle_flow_3_step,
  &ux_idle_flow_4_step,
  FLOW_END_STEP,
};

UX_STEP_VALID(
    ux_settings_flow_1_step,
    bnnn,
    switch_settings_contract_data(),
    {
      "Contract data",
      "Allow contract data",
      "in transactions",
      fullAddress,
    });

UX_STEP_VALID(
    ux_settings_flow_3_step,
    pb,
    ui_idle(),
    {
      &C_icon_back_x,
      "Back",
    });

const ux_flow_step_t *        const ux_settings_flow [] = {
  &ux_settings_flow_1_step,
  &ux_settings_flow_3_step,
  FLOW_END_STEP,
};

void display_settings() {
  strcpy(fullAddress, (N_storage.dataAllowed ? "Allowed" : "NOT Allowed"));
  ux_flow_init(0, ux_settings_flow, NULL);
}

void switch_settings_contract_data() {
  uint8_t value = (N_storage.dataAllowed ? 0 : 1);
  nvm_write(&N_storage.dataAllowed, (void*)&value, sizeof(uint8_t));
  display_settings();
}


//////////////////////////////////////////////////////////////////////
UX_STEP_NOCB(ux_approval_tx_1_step,
    pnn,
    {
      &C_icon_eye,
      "Review",
      "transaction",
    });
UX_STEP_NOCB(
    ux_approval_tx_2_step,
    bnnn_paging,
    {
      .title = "Amount",
      .text = fullAmount
    });
UX_STEP_NOCB(
    ux_approval_tx_3_step,
    bnnn_paging,
    {
      .title = "Address",
      .text = fullAddress,
    });
UX_STEP_NOCB(
    ux_approval_tx_4_step,
    bnnn_paging,
    {
      .title = "Max Fees",
      .text = maxFee,
    });
UX_STEP_VALID(
    ux_approval_tx_5_step,
    pbb,
    io_seproxyhal_touch_tx_ok(NULL),
    {
      &C_icon_validate_14,
      "Accept",
      "and send",
    });
UX_STEP_VALID(
    ux_approval_tx_6_step,
    pb,
    io_seproxyhal_touch_tx_cancel(NULL),
    {
      &C_icon_crossmark,
      "Reject",
    });
UX_STEP_NOCB(ux_approval_tx_data_warning_step,
    pbb,
    {
      &C_icon_warning_x,
      "Data",
      "Present",
    });


const ux_flow_step_t *        const ux_approval_tx_flow [] = {
  &ux_approval_tx_1_step,
  &ux_approval_tx_2_step,
  &ux_approval_tx_3_step,
  &ux_approval_tx_4_step,
  &ux_approval_tx_5_step,
  &ux_approval_tx_6_step,
  FLOW_END_STEP,
};

const ux_flow_step_t *        const ux_approval_tx_data_warning_flow [] = {
  &ux_approval_tx_1_step,
  &ux_approval_tx_data_warning_step,
  &ux_approval_tx_2_step,
  &ux_approval_tx_3_step,
  &ux_approval_tx_4_step,
  &ux_approval_tx_5_step,
  &ux_approval_tx_6_step,
  FLOW_END_STEP,
};


#endif // #if defined(TARGET_NANOX)


void ui_idle(void) {
    skipWarning = false;
#if defined(TARGET_BLUE)
    UX_DISPLAY(ui_idle_blue, ui_idle_blue_prepro);
#elif defined(TARGET_NANOS)
    UX_MENU_DISPLAY(0, menu_main, NULL);   
#elif defined(TARGET_NANOX)
    // reserve a display stack slot if none yet
    if(G_ux.stack_count == 0) {
        ux_stack_push();
    }
    ux_flow_init(0, ux_idle_flow, NULL);
#endif // #if TARGET_ID
}

#if defined(TARGET_BLUE)
unsigned int io_seproxyhal_touch_settings(const bagl_element_t *e) {
  UX_DISPLAY(ui_settings_blue, ui_settings_blue_prepro);
  return 0; // do not redraw button, screen has switched
}
#endif // #if defined(TARGET_BLUE)

unsigned int io_seproxyhal_touch_exit(const bagl_element_t *e) {
    // Go back to the dashboard
    os_sched_exit(0);
    return 0; // do not redraw the widget
}

unsigned int io_seproxyhal_touch_tx_ok(const bagl_element_t *e) {
    uint8_t privateKeyData[32];
    uint8_t signatureLength;
    cx_ecfp_private_key_t privateKey;
    uint32_t tx = 0;
    os_perso_derive_node_bip32_seed_key(HDW_ED25519_SLIP10,
                               CX_CURVE_Ed25519, tmpCtx.transactionContext.bip32Path,
                               tmpCtx.transactionContext.pathLength,
                               privateKeyData, NULL, (unsigned char*) "ed25519 seed", 12);
    cx_ecfp_init_private_key(CX_CURVE_Ed25519, privateKeyData, 32,
                                 &privateKey);
    os_memset(privateKeyData, 0, sizeof(privateKeyData));
    unsigned int info = 0;    

    // get hash
    unsigned char hash[HASH_SIZE];
    cx_blake2b_init(&blake, HASH_SIZE*8);
    cx_hash((cx_hash_t *) &blake, CX_LAST, transaction_buffer, buffer_counter, hash, 32);

    signatureLength =
        cx_eddsa_sign(&privateKey, CX_LAST, CX_SHA512,
                      hash,
                      HASH_SIZE, NULL, 0, G_io_apdu_buffer, 80, &info);
    os_memset(&privateKey, 0, sizeof(privateKey));    

    tx = signatureLength;
    G_io_apdu_buffer[tx++] = 0x90;
    G_io_apdu_buffer[tx++] = 0x00;
send:    
    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
    // Display back the original UX
    ui_idle();
    return 0; // do not redraw the widget
}

unsigned int io_seproxyhal_touch_tx_cancel(const bagl_element_t *e) {
    G_io_apdu_buffer[0] = 0x69;
    G_io_apdu_buffer[1] = 0x85;
    // Send back the response, do not restart the event loop
    io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, 2);
    // Display back the original UX
    ui_idle();
    return 0; // do not redraw the widget
}

#if defined(TARGET_BLUE)
void ui_approval_blue_init(void) {
  UX_DISPLAY(ui_approval_blue, ui_approval_blue_prepro);
}

void ui_approval_transaction_blue_init(void) {
  ui_approval_blue_ok = (bagl_element_callback_t) io_seproxyhal_touch_tx_ok;
  ui_approval_blue_cancel = (bagl_element_callback_t) io_seproxyhal_touch_tx_cancel;
  G_ui_approval_blue_state = APPROVAL_TRANSACTION;
  ui_approval_blue_values[0] = fullAmount;
  ui_approval_blue_values[1] = fullAddress;
  ui_approval_blue_values[2] = maxFee;
  ui_approval_blue_init();
}

#elif defined(TARGET_NANOS)
unsigned int ui_approval_nanos_button(unsigned int button_mask, unsigned int button_mask_counter) {
    switch(button_mask) {
        case BUTTON_EVT_RELEASED|BUTTON_LEFT:
            io_seproxyhal_touch_tx_cancel(NULL);
            break;

        case BUTTON_EVT_RELEASED|BUTTON_RIGHT: {
			      io_seproxyhal_touch_tx_ok(NULL);
            break;
        }
    }    
    return 0;
}


unsigned int ui_approval_signMessage_nanos_button(unsigned int button_mask, unsigned int button_mask_counter) {
    switch (button_mask) {
    case BUTTON_EVT_RELEASED | BUTTON_LEFT:
        io_seproxyhal_touch_signMessage_cancel(NULL);
        break;

    case BUTTON_EVT_RELEASED | BUTTON_RIGHT: {
        io_seproxyhal_touch_signMessage_ok(NULL);
        break;
    }
    }
    return 0;
}

#endif // #if defined(TARGET_NANOS)

unsigned short io_exchange_al(unsigned char channel, unsigned short tx_len) {
    switch (channel & ~(IO_FLAGS)) {
    case CHANNEL_KEYBOARD:
        break;

    // multiplexed io exchange over a SPI channel and TLV encapsulated protocol
    case CHANNEL_SPI:
        if (tx_len) {
            io_seproxyhal_spi_send(G_io_apdu_buffer, tx_len);

            if (channel & IO_RESET_AFTER_REPLIED) {
                reset();
            }
            return 0; // nothing received from the master so far (it's a tx
                      // transaction)
        } else {
            return io_seproxyhal_spi_recv(G_io_apdu_buffer,
                                          sizeof(G_io_apdu_buffer), 0);
        }

    default:
        THROW(INVALID_PARAMETER);
    }
    return 0;
}

uint32_t set_result_get_publicKey(cx_ecfp_public_key_t* pubKey) {
    uint32_t tx = 0;

    // fetch public key
    char publicKey[KEY_SIZE];
    for (int i = 0; i < KEY_SIZE; i++) {
        publicKey[i] = pubKey->W[64 - i];
    }
    if ((pubKey->W[KEY_SIZE] & 1) != 0) {
        publicKey[KEY_SIZE-1] |= 0x80;
    }

    // map address from public key
    unsigned char hash[HASH_SIZE];
    cx_blake2b_init(&blake, HASH_SIZE*8);
    cx_hash((cx_hash_t *) &blake, CX_LAST, publicKey, KEY_SIZE, hash, 32);
    hash[0] = 0xa0;

    // write to output buffer
    os_memmove(G_io_apdu_buffer, publicKey, KEY_SIZE);
    os_memmove(G_io_apdu_buffer+KEY_SIZE, hash, HASH_SIZE);
    tx = KEY_SIZE+HASH_SIZE;

    return tx;
}

void convertUint256BE(uint8_t *data, uint32_t length, uint256_t *target) {
    uint8_t tmp[32];
    os_memset(tmp, 0, 32);
    os_memmove(tmp + 32 - length, data, length);
    readu256BE(tmp, target);
}

bool customProcessor(txContext_t *context) {
    if ((context->currentField == TX_RLP_DATA) &&
        (context->currentFieldLength != 0)) {
          dataPresent = true;
          if (context->currentFieldLength == sizeof(tokenContext.data)) {
              if (context->currentFieldPos < context->currentFieldLength) {
                  uint32_t copySize =
                      (context->commandLength <
                          ((context->currentFieldLength - context->currentFieldPos))
                      ? context->commandLength
                      : context->currentFieldLength - context->currentFieldPos);
                  copyTxData(context, tokenContext.data + context->currentFieldPos,
                        copySize);
              }
              if (context->currentFieldPos == context->currentFieldLength) {
                  context->currentField++;
                  context->processingField = false;
                  // Initial check to see if the token content can be processed
                  tokenContext.provisioned = (os_memcmp(tokenContext.data, TOKEN_TRANSFER_ID, 4) == 0);                   
              }
              return true;
          }
      }
    return false;
}


void handleGetPublicKey(uint8_t p1, uint8_t p2, uint8_t *dataBuffer, uint16_t dataLength, volatile unsigned int *flags, volatile unsigned int *tx) {
  UNUSED(dataLength);  
  uint8_t privateKeyData[32];
  uint32_t bip32Path[MAX_BIP32_PATH];
  uint32_t i;
  uint8_t bip32PathLength = *(dataBuffer++);
  cx_ecfp_private_key_t privateKey;
  cx_ecfp_public_key_t public_key;

  if ((bip32PathLength < 0x01) ||
      (bip32PathLength > MAX_BIP32_PATH)) {
    PRINTF("Invalid path\n");
    THROW(0x6a80);
  }
  if ((p1 != P1_CONFIRM) && (p1 != P1_NON_CONFIRM)) {
    THROW(0x6B00);
  }
  if ((p2 != P2_CHAINCODE) && (p2 != P2_NO_CHAINCODE)) {
    THROW(0x6B00);
  }
  for (i = 0; i < bip32PathLength; i++) {
    bip32Path[i] = (dataBuffer[0] << 24) |
                   (dataBuffer[1] << 16) |
                   (dataBuffer[2] << 8) | (dataBuffer[3]);
    dataBuffer += 4;
  }
  tmpCtx.publicKeyContext.getChaincode = (p2 == P2_CHAINCODE);
  os_perso_derive_node_bip32_seed_key(HDW_ED25519_SLIP10, CX_CURVE_Ed25519, bip32Path, bip32PathLength, privateKeyData, NULL, (unsigned char*) "ed25519 seed", 12);
  cx_ecfp_init_private_key(CX_CURVE_Ed25519, privateKeyData, 32, &privateKey);
  cx_ecfp_generate_pair(CX_CURVE_Ed25519, &public_key, &privateKey, 1);
  os_memset(&privateKey, 0, sizeof(privateKey));
  os_memset(privateKeyData, 0, sizeof(privateKeyData));
  *tx = set_result_get_publicKey(&public_key);
  THROW(0x9000);
}

void handleSign(uint8_t p1, uint8_t p2, uint8_t *workBuffer, uint16_t dataLength, volatile unsigned int *flags, volatile unsigned int *tx) {
  UNUSED(tx);
  parserStatus_e txResult;                    
  uint256_t gasPrice, startGas, uint256;
  uint32_t i;
  uint8_t address[65];
  uint8_t decimals = nAmp_TO_AION;
  uint8_t *ticker = PIC(chainConfig->coinName);
  uint8_t tickerOffset = 0;
  if (p1 == P1_FIRST) {
    tmpCtx.transactionContext.pathLength = workBuffer[0];
    if ((tmpCtx.transactionContext.pathLength < 0x01) ||
        (tmpCtx.transactionContext.pathLength > MAX_BIP32_PATH)) {
      PRINTF("Invalid path\n");
      THROW(0x6a80);
    }
    workBuffer++;
    dataLength--;
    for (i = 0; i < tmpCtx.transactionContext.pathLength; i++) {
      tmpCtx.transactionContext.bip32Path[i] =
        (workBuffer[0] << 24) | (workBuffer[1] << 16) |
        (workBuffer[2] << 8) | (workBuffer[3]);
      workBuffer += 4;
      dataLength -= 4;
    }
    dataPresent = false;
    tokenContext.provisioned = false;

    // concatenate to transaction buffer
    uint16_t i = 0;  
    if(dataLength >= MAX_TRANSACTION_SIZE){
      THROW(0x6A80);
    }
    buffer_counter =  dataLength;      
    for(i=0;i<dataLength;i++){
      transaction_buffer[i] = workBuffer[i];
    }
    transaction_buffer[buffer_counter] = '\0';

    initTx(&txContext, &tmpContent.txContent, customProcessor, NULL);
  } 
  else 
  if (p1 != P1_MORE) {
    THROW(0x6B00);
  }
  if (p2 != 0) {
    THROW(0x6B00);
  }
  if (txContext.currentField == TX_RLP_NONE) {
    PRINTF("Parser not initialized\n");
    THROW(0x6985);
  }
  txResult = processTx(&txContext, workBuffer, dataLength, 0);
  switch (txResult) {
    case USTREAM_FINISHED:
      break;
    case USTREAM_PROCESSING:
      THROW(0x9000);
    case USTREAM_FAULT:
      THROW(0x6A80);
    default:
      PRINTF("Unexpected parser status\n");
      THROW(0x6A80);
  }
  // If there is a token to process, check if it is well known
  if (tokenContext.provisioned) {
    uint32_t numTokens = 0;
    switch(chainConfig->kind) {
      case CHAIN_KIND_AION:
        numTokens = NUM_TOKENS_AION;
        break;
    }
    for (i=0; i<numTokens; i++) {
      tokenDefinition_t *currentToken = NULL;
      switch(chainConfig->kind) {
        case CHAIN_KIND_AION:
          currentToken = PIC(&TOKENS_AION[i]);
          break;
      } 
      if (os_memcmp(currentToken->address, tmpContent.txContent.destination, 32) == 0) {
        dataPresent = false;
        decimals = currentToken->decimals;
        ticker = currentToken->ticker;
        tmpContent.txContent.destinationLength = 32;
        os_memmove(tmpContent.txContent.destination, tokenContext.data + 4 + 12, 32);
        os_memmove(tmpContent.txContent.value.value, tokenContext.data + 4 + 32, 32);
        tmpContent.txContent.value.length = 32;
        break;
      }
    }
  }  
  else {
    if (dataPresent && !N_storage.dataAllowed) {
      PRINTF("Data field forbidden\n");
      THROW(0x6A80);
    } 
  }
  // Add address
  if (tmpContent.txContent.destinationLength != 0) {
    getEthAddressStringFromBinary(tmpContent.txContent.destination, address);

    fullAddress[0] = '0';
    fullAddress[1] = 'x';
    os_memmove((unsigned char *)fullAddress+2, address, 64);
    fullAddress[66] = '\0';
  }
  else 
  {
    os_memmove((void*)addressSummary, CONTRACT_ADDRESS, sizeof(CONTRACT_ADDRESS));
    strcpy(fullAddress, "Contract");
  }
  // Add amount in aion
  convertUint256BE(tmpContent.txContent.value.value, tmpContent.txContent.value.length, &uint256);
  tostring256(&uint256, 10, (char *)(G_io_apdu_buffer + 100), 100);
  i = 0;
  while (G_io_apdu_buffer[100 + i]) {
    i++;
  }
  adjustDecimals((char *)(G_io_apdu_buffer + 100), i, (char *)G_io_apdu_buffer, 100, decimals);
  i = 0;
    tickerOffset = 0;
    while (ticker[tickerOffset]) {
        fullAmount[tickerOffset] = ticker[tickerOffset];
        tickerOffset++;
    }
    while (G_io_apdu_buffer[i]) {
        fullAmount[tickerOffset + i] = G_io_apdu_buffer[i];
        i++;
    }  
  fullAmount[tickerOffset + i] = '\0';
  // Compute maximum fee
  convertUint256BE(tmpContent.txContent.gasprice.value, tmpContent.txContent.gasprice.length, &gasPrice);
  convertUint256BE(tmpContent.txContent.startgas.value, tmpContent.txContent.startgas.length, &startGas);
  mul256(&gasPrice, &startGas, &uint256);
  tostring256(&uint256, 10, (char *)(G_io_apdu_buffer + 100), 100);
  i = 0;
  while (G_io_apdu_buffer[100 + i]) {
    i++;
  }
  adjustDecimals((char *)(G_io_apdu_buffer + 100), i, (char *)G_io_apdu_buffer, 100, nAmp_TO_AION);
  i = 0;
  tickerOffset=0;
  while (ticker[tickerOffset]) {
      maxFee[tickerOffset] = ticker[tickerOffset];
      tickerOffset++;
  }
  tickerOffset++;
  while (G_io_apdu_buffer[i]) {
    maxFee[tickerOffset + i] = G_io_apdu_buffer[i];
    i++;
  }
  maxFee[tickerOffset + i] = '\0';

#if defined(TARGET_BLUE)
  ui_approval_transaction_blue_init();
#elif defined(TARGET_NANOS)
  skipWarning = !dataPresent;
  ux_step = 0;
  ux_step_count = 5;
  UX_DISPLAY(ui_approval_nanos, ui_approval_prepro);   
#elif defined(TARGET_NANOX)
  ux_flow_init(0, (dataPresent ? ux_approval_tx_data_warning_flow : ux_approval_tx_flow), NULL);
#endif // #if TARGET_ID

  *flags |= IO_ASYNCH_REPLY;
}

void handleGetAppConfiguration(uint8_t p1, uint8_t p2, uint8_t *workBuffer, uint16_t dataLength, volatile unsigned int *flags, volatile unsigned int *tx) {
  UNUSED(p1);
  UNUSED(p2);
  UNUSED(workBuffer);
  UNUSED(dataLength);
  UNUSED(flags);
  G_io_apdu_buffer[0] = (N_storage.dataAllowed ? 0x01 : 0x00);
  G_io_apdu_buffer[1] = LEDGER_MAJOR_VERSION;
  G_io_apdu_buffer[2] = LEDGER_MINOR_VERSION;
  G_io_apdu_buffer[3] = LEDGER_PATCH_VERSION;
  *tx = 4;
  THROW(0x9000);
}

void handleApdu(volatile unsigned int *flags, volatile unsigned int *tx) {
  unsigned short sw = 0;

  BEGIN_TRY {
    TRY {
      if (G_io_apdu_buffer[OFFSET_CLA] != CLA) {
        THROW(0x6E00);
      }

      switch (G_io_apdu_buffer[OFFSET_INS]) {
        case INS_GET_PUBLIC_KEY: 
          handleGetPublicKey(G_io_apdu_buffer[OFFSET_P1], G_io_apdu_buffer[OFFSET_P2], G_io_apdu_buffer + OFFSET_CDATA, G_io_apdu_buffer[OFFSET_LC], flags, tx);
          break;

        case INS_SIGN: 
          handleSign(G_io_apdu_buffer[OFFSET_P1], G_io_apdu_buffer[OFFSET_P2], G_io_apdu_buffer + OFFSET_CDATA, G_io_apdu_buffer[OFFSET_LC], flags, tx);
          break;

        case INS_GET_APP_CONFIGURATION: 
          handleGetAppConfiguration(G_io_apdu_buffer[OFFSET_P1], G_io_apdu_buffer[OFFSET_P2], G_io_apdu_buffer + OFFSET_CDATA, G_io_apdu_buffer[OFFSET_LC], flags, tx);
          break;

#if 0
        case 0xFF: // return to dashboard
          goto return_to_dashboard;
#endif          

        default:
          THROW(0x6D00);
          break;
      }
    }
    CATCH(EXCEPTION_IO_RESET) {
      THROW(EXCEPTION_IO_RESET);
    }
    CATCH_OTHER(e) {
      switch (e & 0xF000) {
        case 0x6000:
          // Wipe the transaction context and report the exception
          sw = e;
          os_memset(&txContext, 0, sizeof(txContext));
          break;
        case 0x9000:
          // All is well
          sw = e;
          break;
        default:
          // Internal error
          sw = 0x6800 | (e & 0x7FF);
          break;
        }
        // Unexpected exception => report
        G_io_apdu_buffer[*tx] = sw >> 8;
        G_io_apdu_buffer[*tx + 1] = sw;
        *tx += 2;
      }
      FINALLY {
      }
  }
  END_TRY;
}

void sample_main(void) {
    volatile unsigned int rx = 0;
    volatile unsigned int tx = 0;
    volatile unsigned int flags = 0;

    // DESIGN NOTE: the bootloader ignores the way APDU are fetched. The only
    // goal is to retrieve APDU.
    // When APDU are to be fetched from multiple IOs, like NFC+USB+BLE, make
    // sure the io_event is called with a
    // switch event, before the apdu is replied to the bootloader. This avoid
    // APDU injection faults.
    for (;;) {
        volatile unsigned short sw = 0;

        BEGIN_TRY {
            TRY {
                rx = tx;
                tx = 0; // ensure no race in catch_other if io_exchange throws
                        // an error
                rx = io_exchange(CHANNEL_APDU | flags, rx);
                flags = 0;

                // no apdu received, well, reset the session, and reset the
                // bootloader configuration
                if (rx == 0) {
                    THROW(0x6982);
                }

                PRINTF("New APDU received:\n%.*H\n", rx, G_io_apdu_buffer);

                handleApdu(&flags, &tx);
            }
            CATCH(EXCEPTION_IO_RESET) {
              THROW(EXCEPTION_IO_RESET);
            }
            CATCH_OTHER(e) {
                switch (e & 0xF000) {
                case 0x6000:
                    // Wipe the transaction context and report the exception
                    sw = e;
                    os_memset(&txContext, 0, sizeof(txContext));
                    break;
                case 0x9000:
                    // All is well
                    sw = e;
                    break;
                default:
                    // Internal error
                    sw = 0x6800 | (e & 0x7FF);
                    break;
                }
                // Unexpected exception => report
                G_io_apdu_buffer[tx] = sw >> 8;
                G_io_apdu_buffer[tx + 1] = sw;
                tx += 2;
            }
            FINALLY {
            }
        }
        END_TRY;
    }

//return_to_dashboard:
    return;
}

// override point, but nothing more to do
void io_seproxyhal_display(const bagl_element_t *element) {
  io_seproxyhal_display_default((bagl_element_t *)element);
}

unsigned char io_event(unsigned char channel) {
    // nothing done with the event, throw an error on the transport layer if
    // needed

    // can't have more than one tag in the reply, not supported yet.
    switch (G_io_seproxyhal_spi_buffer[0]) {
    case SEPROXYHAL_TAG_FINGER_EVENT:
    		UX_FINGER_EVENT(G_io_seproxyhal_spi_buffer);
    		break;
		
    case SEPROXYHAL_TAG_BUTTON_PUSH_EVENT:
        UX_BUTTON_PUSH_EVENT(G_io_seproxyhal_spi_buffer);
        break;

    case SEPROXYHAL_TAG_STATUS_EVENT:
        if (G_io_apdu_media == IO_APDU_MEDIA_USB_HID && !(U4BE(G_io_seproxyhal_spi_buffer, 3) & SEPROXYHAL_TAG_STATUS_EVENT_FLAG_USB_POWERED)) {
         THROW(EXCEPTION_IO_RESET);
        }
        // no break is intentional
    default:
        UX_DEFAULT_EVENT();
        break;

    case SEPROXYHAL_TAG_DISPLAY_PROCESSED_EVENT:
        UX_DISPLAYED_EVENT({});
        break;

    case SEPROXYHAL_TAG_TICKER_EVENT:
        UX_TICKER_EVENT(G_io_seproxyhal_spi_buffer, 
        {
          #ifndef TARGET_NANOX
          if (UX_ALLOWED) {
            if (skipWarning && (ux_step == 0)) {
              ux_step++;
            }

            if (ux_step_count) {
              // prepare next screen
              ux_step = (ux_step+1)%ux_step_count;
              // redisplay screen
              UX_REDISPLAY(); 
            }
          }
          #endif // TARGET_NANOX
        });
        break;
    }

    // close the event if not done previously (by a display or whatever)
    if (!io_seproxyhal_spi_is_status_sent()) {
        io_seproxyhal_general_status();
    }

    // command has been processed, DO NOT reset the current APDU transport
    return 1;
}

void app_exit(void) {

    BEGIN_TRY_L(exit) {
        TRY_L(exit) {
            os_sched_exit(-1);
        }
        FINALLY_L(exit) {

        }
    }
    END_TRY_L(exit);
}

chain_config_t const C_chain_config = {
  .coinName = CHAINID_COINNAME " ",
  .chainId = CHAIN_ID,
  .kind = CHAIN_KIND,
#ifdef TARGET_BLUE
  .color_header = COLOR_APP,
  .color_dashboard = COLOR_APP_LIGHT,
  .header_text = CHAINID_UPCASE,
#endif // TARGET_BLUE
};

__attribute__((section(".boot"))) int main(int arg0) { 
    // exit critical section
    __asm volatile("cpsie i");

    if (arg0) {
        if (((unsigned int *)arg0)[0] != 0x100) {
            os_lib_throw(INVALID_PARAMETER);
        }      
        chainConfig = (chain_config_t *)((unsigned int *)arg0)[1];
    }
    else {
        chainConfig = (chain_config_t *)PIC(&C_chain_config);
    }

    os_memset(&txContext, 0, sizeof(txContext));

    // ensure exception will work as planned
    os_boot();

    for (;;) {
        UX_INIT();

        BEGIN_TRY {
            TRY {
                io_seproxyhal_init();

                if (N_storage.initialized != 0x01) {
                  internalStorage_t storage;
                  storage.dataAllowed = 0x01;
                  storage.initialized = 0x01;
                  nvm_write(&N_storage, (void*)&storage, sizeof(internalStorage_t));
                }
                dataAllowed = N_storage.dataAllowed;

                USB_power(0);
                USB_power(1);

    			      ui_idle();

    #if defined(TARGET_BLUE)
                // setup the status bar colors (remembered after wards, even more if another app does not resetup after app switch)
                UX_SET_STATUS_BAR_COLOR(0xFFFFFF, chainConfig->color_header);
    #endif // #if defined(TARGET_BLUE)
    			
                sample_main();
            }
            CATCH(EXCEPTION_IO_RESET) {
              // reset IO and UX before continuing
              continue;
            }
            CATCH_ALL {
              break;
            }
            FINALLY {
            }
        }
        END_TRY;
    }
	  app_exit();
    return 0;
}
