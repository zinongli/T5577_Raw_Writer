#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/popup.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <gui/modules/variable_item_list.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include "t5577_writer_icons.h"
#include <applications/services/storage/storage.h>
#include <applications/services/dialogs/dialogs.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <t5577_config.h>
#include <t5577_writer.h>

#define TAG "T5577 Writer"

// Change this to BACKLIGHT_AUTO if you don't want the backlight to be continuously on.
#define BACKLIGHT_AUTO 1

// Our application menu has 3 items.  You can add more items if you want.
typedef enum {
    T5577WriterSubmenuIndexLoad,
    T5577WriterSubmenuIndexSave,
    T5577WriterSubmenuIndexConfigure,
    T5577WriterSubmenuIndexWrite,
    T5577WriterSubmenuIndexAbout,
} T5577WriterSubmenuIndex;

// Each view is a screen we show the user.
typedef enum {
    T5577WriterViewSubmenu, // The menu when the app starts
    T5577WriterViewTextInput, // Input for configuring text settings
    T5577WriterViewLoad,
    T5577WriterViewSave,
    T5577WriterViewConfigure_i, // The configuration screen
    T5577WriterViewConfigure_e, // The configuration screen
    T5577WriterViewWrite, // The main screen
    T5577WriterViewAbout, // The about screen with directions, link to social channel, etc.
} T5577WriterView;

typedef enum {
    T5577WriterEventIdRedrawScreen = 0, // Custom event to redraw the screen
    T5577WriterEventIdOkPressed = 42, // Custom event to process OK button getting pressed down
} T5577WriterEventId;

typedef struct {
    ViewDispatcher* view_dispatcher; // Switches between our views
    NotificationApp* notifications; // Used for controlling the backlight
    Submenu* submenu; // The application menu
    TextInput* text_input; // The text input screen
    VariableItemList* variable_item_list_config; // The configuration screen
    View* view_config_e; // The configuration screen
    View* view_save; 
    View* view_write; // The main screen
    Widget* widget_about; // The about screen
    View* view_load; // The load view

    VariableItem* mod_item; // 
    VariableItem* clock_item; //
    VariableItem* block_num_item; // 
    VariableItem* block_slc_item; //
    char* temp_buffer; // Temporary buffer for text input
    uint32_t temp_buffer_size; // Size of temporary buffer
    
    DialogsApp* dialogs;
    FuriString* file_path;
    FuriTimer* timer; // Timer for redrawing the screen
} T5577WriterApp;


typedef struct {
    uint8_t modulation_index; // The index for total number of pins
    uint8_t rf_clock_index; // The index for total number of pins
    FuriString* tag_name_str; // The name setting
    uint8_t user_block_num; // The total number of pins we are adjusting
    uint32_t* content; // The cutting content
    t5577_modulation modulation;
    t5577_rf_clock rf_clock;
    bool data_loaded[3];
    uint8_t edit_block_slc;
} T5577WriterModel;

static inline int min(int a, int b) {
    return (a < b) ? a : b;
}

static inline int max(int a, int b) {
    return (a > b) ? a : b;
}

void initialize_config(T5577WriterModel* model) {
    model->modulation_index = 0;
    memcpy(&model->modulation, &all_mods[model->modulation_index], sizeof(t5577_modulation));
    model->rf_clock_index = 0;
    memcpy(&model->rf_clock, &all_mods[model->rf_clock_index], sizeof(t5577_rf_clock));
}

void initialize_model(T5577WriterModel* model) {
    if(model->content != NULL) {
        free(model->content);
    }
    initialize_config(model);
    model->user_block_num = 1;
    model->edit_block_slc = 1;
    model->content = (uint32_t*)malloc(LFRFID_T5577_BLOCK_COUNT * sizeof(uint32_t));
    for(uint32_t i = 0; i < LFRFID_T5577_BLOCK_COUNT; i++) {
        model->content[i] = 0;
    }
    memset(model->data_loaded, false, sizeof(model->data_loaded));
}

uint8_t rf_clock_choices[COUNT_OF(all_rf_clocks)];
void initialize_rf_clock_choices(uint8_t* rf_clock_choices) {
    // Populate the rf_clock_choices array
    for (size_t i = 0; i < COUNT_OF(all_rf_clocks); i++) {
        rf_clock_choices[i] = all_rf_clocks[i].rf_clock_num;
    }
}

char* modulation_names[COUNT_OF(all_mods)];
void initialize_mod_names(char** modulation_names) {
    // Populate the modulation_names array
    for (size_t i = 0; i < COUNT_OF(all_mods); i++) {
        modulation_names[i] = all_mods[i].modulation_name;
    }
}

/**
 * @brief      Callback for exiting the application.
 * @details    This function is called when user press back button.  We return VIEW_NONE to
 *            indicate that we want to exit the application.
 * @param      _context  The context - unused
 * @return     next view id
*/
static uint32_t t5577_writer_navigation_exit_callback(void* _context) {
    UNUSED(_context);
    return VIEW_NONE;
}

/**
 * @brief      Callback for returning to submenu.
 * @details    This function is called when user press back button.  We return VIEW_NONE to
 *            indicate that we want to navigate to the submenu.
 * @param      _context  The context - unused
 * @return     next view id
*/
static uint32_t t5577_writer_navigation_submenu_callback(void* _context) {
    UNUSED(_context);
    return T5577WriterViewSubmenu;
}


/**
 * @brief      Callback for returning to configure screen.
 * @details    This function is called when user press back button.  We return VIEW_NONE to
 *            indicate that we want to navigate to the configure screen.
 * @param      _context  The context - unused
 * @return     next view id
*/
//static uint32_t t5577_writer_navigation_configure_callback(void* _context) {
//    UNUSED(_context);
//    return T5577WriterViewConfigure_i;
//}

static uint32_t t5577_writer_navigation_file_callback(void* _context) {
    UNUSED(_context);
    return T5577WriterViewSubmenu;
}

/**
 * @brief      Handle submenu item selection.
 * @details    This function is called when user selects an item from the submenu.
 * @param      context  The context - T5577WriterApp object.
 * @param      index     The T5577WriterSubmenuIndex item that was clicked.
*/
static void t5577_writer_submenu_callback(void* context, uint32_t index) {
    T5577WriterApp* app = (T5577WriterApp*)context;
    switch(index) {
    case T5577WriterSubmenuIndexLoad:
        view_dispatcher_switch_to_view(app->view_dispatcher, T5577WriterViewLoad);
        break;
    case T5577WriterSubmenuIndexSave:
        view_dispatcher_switch_to_view(app->view_dispatcher, T5577WriterViewSave);
        break;
    case T5577WriterSubmenuIndexConfigure:
        view_dispatcher_switch_to_view(app->view_dispatcher, T5577WriterViewConfigure_e);
        break;
    case T5577WriterSubmenuIndexWrite:
        view_dispatcher_switch_to_view(app->view_dispatcher, T5577WriterViewWrite);
        break;
    case T5577WriterSubmenuIndexAbout:
        view_dispatcher_switch_to_view(app->view_dispatcher, T5577WriterViewAbout);
        break;
    default:
        break;
    }
}

/**
 * Our 1st sample setting is a team color.  We have 3 options: red, green, and blue.
*/
static const char* modulation_config_label = "Modulation";
//static char* modulation_names[] = {"Direct", "PSK1", "PSK2", "PSK3", "FSK1", "FSK2", "FSK1a", "FSK2a","ASK/Man","Biphase","Diphase"};
static void t5577_writer_modulation_change(VariableItem* item) {
    T5577WriterApp* app = variable_item_get_context(item);
    FURI_LOG_D(TAG,"app defined");
    T5577WriterModel* model = view_get_model(app->view_write);
    FURI_LOG_D(TAG,"model defined");
    if (model->data_loaded[0]) {
        FURI_LOG_D(TAG,"loaded entered");
        variable_item_set_current_value_index(item,model->modulation_index);
    } else{
        FURI_LOG_D(TAG,"else entered");
        uint8_t modulation_index = variable_item_get_current_value_index(item);
        model->modulation_index = modulation_index;
        model->modulation = all_mods[modulation_index];
    }
    model->data_loaded[0] = false;
    variable_item_set_current_value_text(item, modulation_names[model->modulation_index]);
}

static const char* rf_clock_config_label = "RF Clock";
static void t5577_writer_rf_clock_change(VariableItem* item) {
    T5577WriterApp* app = variable_item_get_context(item);
    T5577WriterModel* model = view_get_model(app->view_write);

    if (model->data_loaded[1]) {
        variable_item_set_current_value_index(item,model->rf_clock_index);
    } else{
        uint8_t rf_clock_index = variable_item_get_current_value_index(item);
        model->rf_clock_index = rf_clock_index;
        model->rf_clock = all_rf_clocks[rf_clock_index];
        
    }
    model->data_loaded[1] = false;
    FuriString *buffer = furi_string_alloc();
    furi_string_printf(buffer, "%u", rf_clock_choices[model->rf_clock_index]);
    variable_item_set_current_value_text(item, furi_string_get_cstr(buffer));
    furi_string_free(buffer);
}

static const char* user_block_num_config_label = "Num of Blocks";
static void t5577_writer_user_block_num_change(VariableItem* item) {
    T5577WriterApp* app = variable_item_get_context(item);
    T5577WriterModel* model = view_get_model(app->view_write);
    if (model->data_loaded[2]) {
        variable_item_set_current_value_index(item,model->user_block_num - 1);
    } else {
        uint8_t user_block_num_index = variable_item_get_current_value_index(item);
        model->user_block_num = user_block_num_index + 1;
    }
    model->data_loaded[2] = false;
    FuriString *buffer = furi_string_alloc();
    furi_string_printf(buffer, "%u", model->user_block_num);
    variable_item_set_current_value_text(item, furi_string_get_cstr(buffer));
    for(uint8_t i = model->user_block_num; i < LFRFID_T5577_BLOCK_COUNT; i++) {
        model->content[i] = 0;
    }
    furi_string_free(buffer);
}

static const char* edit_block_slc_config_label = "Edit Block";
static void t5577_writer_edit_block_slc_change(VariableItem* item) {
    T5577WriterApp* app = variable_item_get_context(item);
    T5577WriterModel* model = view_get_model(app->view_write);
    uint8_t edit_block_slc_index = variable_item_get_current_value_index(item);
    model->edit_block_slc = edit_block_slc_index + 1;
    variable_item_set_current_value_index(item,model->edit_block_slc - 1);
    FuriString *buffer = furi_string_alloc();
    furi_string_printf(buffer, "%u", model->edit_block_slc);
    variable_item_set_current_value_text(item, furi_string_get_cstr(buffer));
    furi_string_free(buffer);
}

void ensure_dir_exists(Storage *storage)
{
    // If apps_data directory doesn't exist, create it.
    if (!storage_dir_exists(storage, T5577_WRITER_APPS_DATA_FOLDER))
    {
        FURI_LOG_I(TAG, "Creating directory: %s", T5577_WRITER_APPS_DATA_FOLDER);
        storage_simply_mkdir(storage, T5577_WRITER_APPS_DATA_FOLDER);
    }
    else
    {
        FURI_LOG_I(TAG, "Directory exists: %s", T5577_WRITER_APPS_DATA_FOLDER);
    }

    // If wiegand directory doesn't exist, create it.
    if (!storage_dir_exists(storage, T5577_WRITER_FILE_FOLDER))
    {
        FURI_LOG_I(TAG, "Creating directory: %s", T5577_WRITER_FILE_FOLDER);
        storage_simply_mkdir(storage, T5577_WRITER_FILE_FOLDER);
    }
    else
    {
        FURI_LOG_I(TAG, "Directory exists: %s", T5577_WRITER_FILE_FOLDER);
    }
}
/**
 * Our 2nd sample setting is a text field.  When the user clicks OK on the configuration 
 * setting we use a text input screen to allow the user to enter a name.  This function is
 * called when the user clicks OK on the text input screen.
*/
static const char* tag_name_entry_text = "Enter name";
static const char* tag_name_default_value = "Tag_1";
static void t5577_writer_file_saver(void* context) {
    T5577WriterApp* app = (T5577WriterApp*)context;
    T5577WriterModel* model = view_get_model(app->view_write);
    model->content[0] = 0;
    model->content[0] |= model->modulation.mod_page_zero;
    model->content[0] |= model->rf_clock.clock_page_zero;
    model->content[0] |= (model->user_block_num << LFRFID_T5577_MAXBLOCK_SHIFT);
    bool redraw = true;
    with_view_model(
        app->view_write,
        T5577WriterModel * model,
        {
            furi_string_set(model->tag_name_str, app->temp_buffer);
        },
        redraw);
    FuriString *buffer = furi_string_alloc();
    FuriString *file_path = furi_string_alloc();
    furi_string_printf(
        file_path, "%s/%s%s", T5577_WRITER_FILE_FOLDER, furi_string_get_cstr(model->tag_name_str), T5577_WRITER_FILE_EXTENSION);

    Storage *storage = furi_record_open(RECORD_STORAGE);
    ensure_dir_exists(storage);
    File *data_file = storage_file_alloc(storage);
    if (storage_file_open(
            data_file, furi_string_get_cstr(file_path), FSAM_WRITE, FSOM_OPEN_ALWAYS))
    {
        furi_string_printf(buffer, "Filetype: Flipper T5577 Raw File\n");
        storage_file_write(data_file, furi_string_get_cstr(buffer), furi_string_size(buffer));
        furi_string_printf(buffer, "Version: 1.0\n");
        storage_file_write(data_file, furi_string_get_cstr(buffer), furi_string_size(buffer));
        furi_string_printf(buffer, "Modulation: %s\n", model->modulation.modulation_name);
        storage_file_write(data_file, furi_string_get_cstr(buffer), furi_string_size(buffer));
        furi_string_printf(buffer, "RF Clock: %u\n", model->rf_clock.rf_clock_num);
        storage_file_write(data_file, furi_string_get_cstr(buffer), furi_string_size(buffer));
        furi_string_printf(buffer, "Number of User Blocks: %u\n", model->user_block_num);
        storage_file_write(data_file, furi_string_get_cstr(buffer), furi_string_size(buffer));
        furi_string_printf(buffer, "\nRaw Data:\n");
        for (int i = 0; i < model->user_block_num; i++)
        {   
            furi_string_cat_printf(
                buffer,
                "Block %u: %08lX\n",
                i,
                model->content[i]);
        
        }
        furi_string_push_back(buffer, '\n');
        storage_file_write(data_file, furi_string_get_cstr(buffer), furi_string_size(buffer));
        storage_file_close(data_file);
    view_dispatcher_switch_to_view(app->view_dispatcher, T5577WriterViewSubmenu);
    }
}

void t5577_writer_update_config_from_load(void* context) {
    T5577WriterApp* app = (T5577WriterApp*)context;
    T5577WriterModel* my_model = view_get_model(app->view_write);

    
    for (size_t i = 0; i < COUNT_OF(all_mods); i++) {
        if ((my_model->content[0] & all_mods[i].mod_page_zero) == all_mods[i].mod_page_zero) {
            my_model->modulation_index = (uint8_t)i;
            my_model->modulation = all_mods[my_model->modulation_index];
        }
    }

    for (size_t i = 0; i < COUNT_OF(all_rf_clocks); i++) {
        if ((my_model->content[0] & all_rf_clocks[i].clock_page_zero) == all_rf_clocks[i].clock_page_zero) {
            my_model->rf_clock_index = (uint8_t)i;
            my_model->rf_clock = all_rf_clocks[my_model->rf_clock_index];
        }
    }

    my_model->user_block_num = (my_model->content[0] >> LFRFID_T5577_MAXBLOCK_SHIFT) & 0x7;

    memset(my_model->data_loaded, true, sizeof(my_model->data_loaded));

}

static void t5577_writer_config_enter_callback(void* context) {
    T5577WriterApp* app = (T5577WriterApp*)context;

    variable_item_list_reset(app->variable_item_list_config);
    app->mod_item = variable_item_list_add(
        app->variable_item_list_config,
        modulation_config_label,
        COUNT_OF(modulation_names),
        t5577_writer_modulation_change,
        app);
    app->clock_item = variable_item_list_add(
        app->variable_item_list_config,
        rf_clock_config_label,
        COUNT_OF(rf_clock_choices),
        t5577_writer_rf_clock_change,
        app);
    app->block_num_item = variable_item_list_add(
        app->variable_item_list_config,
        user_block_num_config_label,
        LFRFID_T5577_BLOCK_COUNT,
        t5577_writer_user_block_num_change,
        app);
    app->block_slc_item = variable_item_list_add(
        app->variable_item_list_config,
        edit_block_slc_config_label,
        LFRFID_T5577_BLOCK_COUNT - 1,
        t5577_writer_edit_block_slc_change,
        app);
    View* view_config_i = variable_item_list_get_view(app->variable_item_list_config);
    t5577_writer_modulation_change(app->mod_item);
    t5577_writer_rf_clock_change(app->clock_item);
    t5577_writer_user_block_num_change(app->block_num_item);
    t5577_writer_edit_block_slc_change(app->block_slc_item);
    view_set_previous_callback(
        view_config_i,
        t5577_writer_navigation_submenu_callback);
    view_dispatcher_remove_view(app->view_dispatcher, T5577WriterViewConfigure_i);
    view_dispatcher_add_view(
        app->view_dispatcher,
        T5577WriterViewConfigure_i,
        view_config_i);
    view_dispatcher_switch_to_view(app->view_dispatcher,T5577WriterViewConfigure_i);
    FURI_LOG_D(TAG,"enter_callback_finished");
}

void t5577_writer_view_load_callback(void* context) {
    T5577WriterApp* app = (T5577WriterApp*)context;
    T5577WriterModel* model = view_get_model(app->view_write);
    DialogsFileBrowserOptions browser_options;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    ensure_dir_exists(storage);
    File* data_file = storage_file_alloc(storage);
    dialog_file_browser_set_basic_options(&browser_options, T5577_WRITER_FILE_EXTENSION, NULL);
    browser_options.base_path = T5577_WRITER_FILE_FOLDER;
    furi_string_set(app->file_path, browser_options.base_path);
    FuriString* buffer = furi_string_alloc();

    if(dialog_file_browser_show(app->dialogs, app->file_path, app->file_path, &browser_options)) {
        
        if(storage_file_open(
               data_file, furi_string_get_cstr(app->file_path), FSAM_READ, FSOM_OPEN_EXISTING)) {
            while(!storage_file_eof(data_file)) { // fill buffer with every line because ch == '\n'
                char ch;
                furi_string_reset(buffer);
                while(storage_file_read(data_file, &ch, 1) && !storage_file_eof(data_file)) {
                    furi_string_push_back(buffer, ch);
                    if(ch == '\n') {
                        break;
                    }
                }
                if(furi_string_start_with(buffer, "Block ")) {
                    uint32_t row_data_buffer = 0;
                    char row_data_char_buffer[] = "00000000";
                    uint32_t row_num_buffer = 0;
                    char row_num_char_buffer[] = "0";
                    int length = furi_string_size(buffer);
                    char ch;
                    int i = 0;
                    while(i < length) {
                        if(furi_string_get_char(buffer, i) == ':') {
                            row_num_char_buffer[0] = furi_string_get_char(buffer, i - 1); //the number before ":" is block num
                            i += 2; // skip a space
                            for(size_t j = 0; j < sizeof(row_data_char_buffer); j++) {
                                ch = furi_string_get_char(buffer, i);
                                row_data_char_buffer[j] = ch;
                                i++;
                            }
                            break;
                        }
                        i++;
                    }
                    sscanf(row_num_char_buffer, "%lx", &row_num_buffer);
                    sscanf(row_data_char_buffer, "%lx", &row_data_buffer);
                    model->content[row_num_buffer] = row_data_buffer;
                }
            }
            storage_file_close(data_file);
            t5577_writer_update_config_from_load(app);
        }
        storage_file_free(data_file);
        furi_record_close(RECORD_STORAGE);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, T5577WriterViewSubmenu);
}


/**
 * @brief      Callback when item in configuration screen is clicked.
 * @details    This function is called when user clicks OK on an item in the configuration screen.
 *            If the item clicked is our text field then we switch to the text input screen.
 * @param      context  The context - T5577WriterApp object.
 * @param      index - The index of the item that was clicked.
*/
static void t5577_writer_view_save_callback(void* context) {
    T5577WriterApp* app = (T5577WriterApp*)context;
    // Header to display on the text input screen.
    text_input_set_header_text(app->text_input, tag_name_entry_text);
    // Copy the current name into the temporary buffer.
    bool redraw = false;
    with_view_model(
        app->view_write,
        T5577WriterModel * model,
        {
            strncpy(
                app->temp_buffer,
                furi_string_get_cstr(model->tag_name_str),
                app->temp_buffer_size);
        },
        redraw);
    // Configure the text input.  When user enters text and clicks OK, t5577_writer_setting_text_updated be called.
    bool clear_previous_text = false;
    text_input_set_result_callback(
        app->text_input,
        t5577_writer_file_saver,
        app,
        app->temp_buffer,
        app->temp_buffer_size,
        clear_previous_text);
    // Pressing the BACK button will reload the configure screen.
    view_set_previous_callback(
        text_input_get_view(app->text_input), t5577_writer_navigation_file_callback);

    // Show text input dialog.
    view_dispatcher_switch_to_view(app->view_dispatcher, T5577WriterViewTextInput);
    
}

/**
 * @brief      Callback for drawing the game screen.
 * @details    This function is called when the screen needs to be redrawn, like when the model gets updated.
 * @param      canvas  The canvas to draw on.
 * @param      model   The model - MyModel object.
*/
static void t5577_writer_view_write_callback(Canvas* canvas, void* model) {
    T5577WriterModel* my_model = (T5577WriterModel*)model;
    my_model->content[0] = 0;
    my_model->content[0] |= my_model->modulation.mod_page_zero;
    my_model->content[0] |= my_model->rf_clock.clock_page_zero;
    my_model->content[0] |= (my_model->user_block_num << LFRFID_T5577_MAXBLOCK_SHIFT);

    LFRFIDT5577* data = (LFRFIDT5577*)malloc(sizeof(LFRFIDT5577));
    data->blocks_to_write = my_model->user_block_num;
    for(size_t i = 0; i < data->blocks_to_write; i++) {
        data->block[i] = my_model->content[i];
    }
    data->block[1] = 0x007F9FB6;
    data->block[2] = 0xB59FD7F9;
    data->block[3] = 0x006BDFAE;
    data->block[4] = 0xACB83B53;
    t5577_write(data);
    free(data);

    canvas_draw_line(canvas, 0, 25, 120, 50);
}

/**
 * @brief      Callback for timer elapsed.
 * @details    This function is called when the timer is elapsed.  We use this to queue a redraw event.
 * @param      context  The context - T5577WriterApp object.
*/
static void t5577_writer_view_write_timer_callback(void* context) {
    T5577WriterApp* app = (T5577WriterApp*)context;
    view_dispatcher_send_custom_event(app->view_dispatcher, T5577WriterEventIdRedrawScreen);
}

/**
 * @brief      Callback when the user starts the game screen.
 * @details    This function is called when the user enters the game screen.  We start a timer to
 *           redraw the screen periodically (so the random number is refreshed).
 * @param      context  The context - T5577WriterApp object.
*/
static void t5577_writer_view_write_enter_callback(void* context) {
    uint32_t period = furi_ms_to_ticks(500);
    T5577WriterApp* app = (T5577WriterApp*)context;
    furi_assert(app->timer == NULL);
    app->timer =
        furi_timer_alloc(t5577_writer_view_write_timer_callback, FuriTimerTypePeriodic, context);
    furi_timer_start(app->timer, period);
}

/**
 * @brief      Callback when the user exits the game screen.
 * @details    This function is called when the user exits the game screen.  We stop the timer.
 * @param      context  The context - T5577WriterApp object.
*/
static void t5577_writer_view_write_exit_callback(void* context) {
    T5577WriterApp* app = (T5577WriterApp*)context;
    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    app->timer = NULL;
}

/**
 * @brief      Callback for custom events.
 * @details    This function is called when a custom event is sent to the view dispatcher.
 * @param      event    The event id - T5577WriterEventId value.
 * @param      context  The context - T5577WriterApp object.
*/
static bool t5577_writer_view_write_custom_event_callback(uint32_t event, void* context) {
    T5577WriterApp* app = (T5577WriterApp*)context;
    switch(event) {
    case T5577WriterEventIdRedrawScreen:
        // Redraw screen by passing true to last parameter of with_view_model.
        {
            bool redraw = true;
            with_view_model(
                app->view_write, T5577WriterModel * _model, { UNUSED(_model); }, redraw);
            return true;
        }
    case T5577WriterEventIdOkPressed:
        // Process the OK button.  We go to the saving scene.
        view_dispatcher_switch_to_view(app->view_dispatcher, T5577WriterViewSubmenu);
        return true;
    default:
        return false;
    }
}

/**
 * @brief      Callback for game screen input.
 * @details    This function is called when the user presses a button while on the game screen.
 * @param      event    The event - InputEvent object.
 * @param      context  The context - T5577WriterApp object.
 * @return     true if the event was handled, false otherwise.
*/
static bool t5577_writer_view_write_input_callback(InputEvent* event, void* context) {
    T5577WriterApp* app = (T5577WriterApp*)context;
    if(event->type == InputTypeShort) {
        if(event->key == InputKeyOk) {
        // We choose to send a custom event when user presses OK button.  t5577_writer_custom_event_callback will
        // handle our T5577WriterEventIdOkPressed event.  We could have just put the code from
        // t5577_writer_custom_event_callback here, it's a matter of preference.
        view_dispatcher_send_custom_event(app->view_dispatcher, T5577WriterEventIdOkPressed);
        return true;
        }
    }
    return false;
}

/**
 * @brief      Allocate the t5577_writer application.
 * @details    This function allocates the t5577_writer application resources.
 * @return     T5577WriterApp object.
*/
static T5577WriterApp* t5577_writer_app_alloc() {
    T5577WriterApp* app = (T5577WriterApp*)malloc(sizeof(T5577WriterApp));

    Gui* gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->file_path = furi_string_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    app->submenu = submenu_alloc();
    submenu_add_item(
        app->submenu, "Write", T5577WriterSubmenuIndexWrite, t5577_writer_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Config", T5577WriterSubmenuIndexConfigure, t5577_writer_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Save", T5577WriterSubmenuIndexSave, t5577_writer_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Load", T5577WriterSubmenuIndexLoad, t5577_writer_submenu_callback, app);
    submenu_add_item(
        app->submenu, "About", T5577WriterSubmenuIndexAbout, t5577_writer_submenu_callback, app);
    view_set_previous_callback(submenu_get_view(app->submenu), t5577_writer_navigation_exit_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, T5577WriterViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, T5577WriterViewSubmenu);

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, T5577WriterViewTextInput, text_input_get_view(app->text_input));


    app->view_load = view_alloc();
    view_set_previous_callback(app->view_load, t5577_writer_navigation_submenu_callback);
    view_set_enter_callback(app->view_load, t5577_writer_view_load_callback);
    view_set_context(app->view_load, app);
    view_dispatcher_add_view(
        app->view_dispatcher,
        T5577WriterViewLoad,
        app->view_load);

    app->temp_buffer_size = 32;
    app->temp_buffer = (char*)malloc(app->temp_buffer_size);
    
    
    app->view_write = view_alloc();
    view_set_draw_callback(app->view_write, t5577_writer_view_write_callback);
    view_set_input_callback(app->view_write, t5577_writer_view_write_input_callback);
    view_set_previous_callback(app->view_write, t5577_writer_navigation_submenu_callback);
    view_set_enter_callback(app->view_write, t5577_writer_view_write_enter_callback);
    view_set_exit_callback(app->view_write, t5577_writer_view_write_exit_callback);
    view_set_context(app->view_write, app);
    view_set_custom_callback(app->view_write, t5577_writer_view_write_custom_event_callback);
    view_allocate_model(app->view_write, ViewModelTypeLockFree, sizeof(T5577WriterModel));
    view_dispatcher_add_view(app->view_dispatcher, T5577WriterViewWrite, app->view_write);

    T5577WriterModel* model = view_get_model(app->view_write); // initialize model

    FuriString* tag_name_str = furi_string_alloc();
    furi_string_set_str(tag_name_str, tag_name_default_value);
    
    model->tag_name_str = tag_name_str;
    initialize_model(model);
    initialize_rf_clock_choices(rf_clock_choices);
    initialize_mod_names(modulation_names);

    app->view_save = view_alloc();
    view_set_previous_callback(app->view_save, t5577_writer_navigation_submenu_callback);
    view_set_enter_callback(app->view_save, t5577_writer_view_save_callback);
    view_set_context(app->view_save, app);
    view_dispatcher_add_view(
        app->view_dispatcher,
        T5577WriterViewSave,
        app->view_save);


    app->variable_item_list_config = variable_item_list_alloc();

    app->view_config_e = view_alloc();
    view_set_previous_callback(
        app->view_config_e,
        t5577_writer_navigation_submenu_callback);
    view_set_enter_callback(app->view_config_e, t5577_writer_config_enter_callback);
    view_set_context(app->view_config_e, app);
    view_dispatcher_add_view(
        app->view_dispatcher,
        T5577WriterViewConfigure_e,
        app->view_config_e);
        
    View* view_buffer = view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        T5577WriterViewConfigure_i,
        view_buffer);
    

    app->widget_about = widget_alloc();
    widget_add_text_scroll_element(
        app->widget_about,
        0,
        0,
        128,
        64,
        "T5577 Writer v0.1");
    view_set_previous_callback(
        widget_get_view(app->widget_about), t5577_writer_navigation_submenu_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, T5577WriterViewAbout, widget_get_view(app->widget_about));

    app->notifications = furi_record_open(RECORD_NOTIFICATION);

#ifdef BACKLIGHT_ON
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
#endif

    return app;
}

/**
 * @brief      Free the t5577_writer application.
 * @details    This function frees the t5577_writer application resources.
 * @param      app  The t5577_writer application object.
*/
static void t5577_writer_app_free(T5577WriterApp* app) {
#ifdef BACKLIGHT_ON
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
#endif
    furi_record_close(RECORD_NOTIFICATION);

    view_dispatcher_remove_view(app->view_dispatcher, T5577WriterViewTextInput);
    text_input_free(app->text_input);
    free(app->temp_buffer);
    view_dispatcher_remove_view(app->view_dispatcher, T5577WriterViewAbout);
    widget_free(app->widget_about);
    view_dispatcher_remove_view(app->view_dispatcher, T5577WriterViewWrite);
    with_view_model(
        app->view_write,
        T5577WriterModel * model,
        {
            if(model->content != NULL) {
                free(model->content);
            }
        },
        false);
    view_free(app->view_write);
    view_dispatcher_remove_view(app->view_dispatcher, T5577WriterViewLoad);
    view_free(app->view_load);
    view_dispatcher_remove_view(app->view_dispatcher, T5577WriterViewConfigure_i);
    view_dispatcher_remove_view(app->view_dispatcher, T5577WriterViewConfigure_e);
    variable_item_list_free(app->variable_item_list_config);
    view_dispatcher_remove_view(app->view_dispatcher, T5577WriterViewSave);
    view_free(app->view_save);
    view_dispatcher_remove_view(app->view_dispatcher, T5577WriterViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    free(app);
}

/**
 * @brief      Main function for t5577_writer application.
 * @details    This function is the entry point for the t5577_writer application.  It should be defined in
 *           application.fam as the entry_point setting.
 * @param      _p  Input parameter - unused
 * @return     0 - Success
*/
int32_t main_t5577_writer_app(void* _p) {
    UNUSED(_p);

    T5577WriterApp* app = t5577_writer_app_alloc();

    view_dispatcher_run(app->view_dispatcher);

    t5577_writer_app_free(app);
    return 0;
}