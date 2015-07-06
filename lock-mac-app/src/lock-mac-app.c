#include <pebble.h>

#define PRIMARY_COLOR GColorCadetBlue
#define SECONDARY_COLOR GColorWhite

enum {
    KEY_CONNECTED   = 0,
    KEY_LOCKED  = 1,
    KEY_APP  = 2,
    KEY_COMMAND = 3,
};

static Window *window;
static StatusBarLayer *status_bar_layer;
static MenuLayer *menu_layer;

typedef struct SystemDetails {
  char system_name[64];
  uint8_t system_status;
  uint8_t system_lock_status;
} SystemDetails;

static SystemDetails system_details;

// APP Message Callback Functions

void send_command(char * app, char * command) {
  Tuplet app_tup = TupletCString(KEY_APP, app);
  Tuplet command_tup = TupletCString(KEY_COMMAND, command);

  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

  dict_write_tuplet(iter, &app_tup);
  dict_write_tuplet(iter, &command_tup);
  dict_write_end(iter);
  APP_LOG(APP_LOG_LEVEL_INFO, "SENDING COMMAND");
  app_message_outbox_send();
}

void send_command_lock_mac() {
  send_command("System", "lock");
}

void send_command_system_update() {
  send_command("System", "update");
}

void system_update_ui(DictionaryIterator *iter) {

  Tuple *tuple = dict_read_first(iter);

  while(tuple) {
    switch(tuple->key) {
      case KEY_CONNECTED:
        break;
        break;
      case KEY_LOCKED:
        APP_LOG(APP_LOG_LEVEL_INFO, "Receiving system lock status: %d", tuple->value->uint8);
        system_details.system_lock_status = tuple->value->uint8;
        break;
      default:
        break;
    }
    tuple = dict_read_next(iter);
  }
  layer_mark_dirty(menu_layer_get_layer(menu_layer));
}

static void inbox_received_callback(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "AppMessage - Message Received");
  Tuple *app = dict_find(iter, KEY_APP);

  if (app) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "AppMessage - Message for %s", app->value->cstring);

    if (!strcmp("System", app->value->cstring)) {
      system_update_ui(iter);
    }
    return;
  }

  Tuple *connected = dict_find(iter, KEY_CONNECTED);
  if (connected) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "AppMessage - Connection status");
    system_details.system_status = 1;
  } else {
    system_details.system_status = 0;
  }
  layer_mark_dirty(menu_layer_get_layer(menu_layer));
}

static char *translate_error(AppMessageResult result) {
  switch (result) {
    case APP_MSG_OK: return "APP_MSG_OK";
    case APP_MSG_SEND_TIMEOUT: return "APP_MSG_SEND_TIMEOUT";
    case APP_MSG_SEND_REJECTED: return "APP_MSG_SEND_REJECTED";
    case APP_MSG_NOT_CONNECTED: return "APP_MSG_NOT_CONNECTED";
    case APP_MSG_APP_NOT_RUNNING: return "APP_MSG_APP_NOT_RUNNING";
    case APP_MSG_INVALID_ARGS: return "APP_MSG_INVALID_ARGS";
    case APP_MSG_BUSY: return "APP_MSG_BUSY";
    case APP_MSG_BUFFER_OVERFLOW: return "APP_MSG_BUFFER_OVERFLOW";
    case APP_MSG_ALREADY_RELEASED: return "APP_MSG_ALREADY_RELEASED";
    case APP_MSG_CALLBACK_ALREADY_REGISTERED: return "APP_MSG_CALLBACK_ALREADY_REGISTERED";
    case APP_MSG_CALLBACK_NOT_REGISTERED: return "APP_MSG_CALLBACK_NOT_REGISTERED";
    case APP_MSG_OUT_OF_MEMORY: return "APP_MSG_OUT_OF_MEMORY";
    case APP_MSG_CLOSED: return "APP_MSG_CLOSED";
    case APP_MSG_INTERNAL_ERROR: return "APP_MSG_INTERNAL_ERROR";
    default: return "UNKNOWN ERROR";
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Incoming AppMessage from Pebble dropped, %s", translate_error(reason));
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

///////// Menu Layer Callbacks
static uint16_t ml_get_num_rows_cb(struct MenuLayer *menu_layer, uint16_t section_index,
                                   void *callback_context) {
  return 4;
}

static void ml_draw_row_cb(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context) {
  char status[16] = "Unknown";
  char locked[16] = "Unknown";

  switch(cell_index->row) {
    case 0:
      if (system_details.system_status == 1) {
        strcpy(status, "Online");
      } else if (system_details.system_status == 0) {
        strcpy(status, "Offline");
      }
      menu_cell_basic_draw(ctx, cell_layer, "System Status", status, NULL);
      break;
    case 1:
      if (system_details.system_status == 1) {
        if (system_details.system_lock_status == 1) {
          strcpy(locked, "Yes");
        } else {
          strcpy(locked, "No");
        }
      }
      menu_cell_basic_draw(ctx, cell_layer, "Mac Locked?", locked, NULL);
      break;
    case 2:
      menu_cell_basic_draw(ctx, cell_layer, "Lock Now", NULL, NULL);
    default:
      break;
  }
}

static void ml_select_click_cb(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "SELECT CB");
  switch(cell_index->row) {
    case 2:
      send_command_lock_mac();
      break;
    default:
      break;
  }
}

///////// Window Handlers

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  status_bar_layer = status_bar_layer_create();
  status_bar_layer_set_colors(status_bar_layer, PRIMARY_COLOR, SECONDARY_COLOR);
  status_bar_layer_set_separator_mode(status_bar_layer, StatusBarLayerSeparatorModeDotted);

  menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = ml_get_num_rows_cb,
    .draw_row = ml_draw_row_cb,
    .select_click = ml_select_click_cb,
  });

  // Bind the menu layer's click config provider to the window for interactivity
  menu_layer_set_click_config_onto_window(menu_layer, window);

  // Add it to the window for display
  layer_add_child(window_layer, menu_layer_get_layer(menu_layer));
  send_command_system_update();
}

static void window_unload(Window *window) {
  // Destroy all layers here
  window_destroy(window);
  status_bar_layer_destroy(status_bar_layer);
  menu_layer_destroy(menu_layer);
}

static void init(void) {
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  // Open AppMessage
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  const bool animated = true;
  strcpy(system_details.system_name, "");
  system_details.system_status = 0;
  window_stack_push(window, animated);
}

static void deinit(void) {
  window_destroy(window);
}

int main(void) {
  init();

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

  app_event_loop();
  deinit();
}
