#include "http.h"
#include "unistd.h"
#include <stdio.h>
#include <websockets.h>

void print_msg(fio_str_info_s *msg, char *prefix, char *action) {
  printf("%s %s: [ ", prefix, action);
  for (int i = 0; i < msg->len; i++) {
    printf("%c (%d)", msg->data[i], msg->data[i]);
    if (i + 1 < msg->len) {
      printf(", ");
    }
  }
  printf(" ]\n");
}

void on_open(ws_s *ws) {
  char *name = websocket_udata_get(ws);
  printf("%s: Connected to WebSocket server!\n", name);
  // printf("Sending message...\n");

  // char data[] = {4, 4, 'J', 'o', 's', 'e', 4, 'h', 'o', 'l', 'a'};
  // fio_str_info_s message = {.data = data, .len = 11};
  //
  // if (-1 == websocket_write(ws, message, 0)) {
  //   printf("FAILED TO SEND MESSAGE!");
  // }
}

void on_message(ws_s *ws, fio_str_info_s msg, uint8_t is_text) {
  print_msg(&msg, websocket_udata_get(ws), "Received");
}

void on_close(intptr_t uuid, void *udata) {
  printf("WebSocket connection closed.\n");
}

void on_open_2(ws_s *ws) {
  struct timespec time = {.tv_nsec = 0, .tv_sec = 5};
  nanosleep(&time, NULL);
  char *name = websocket_udata_get(ws);
  printf("%s: Connected to WebSocket server!\n", name);
  char data[] = {4, 6, 'F', 'l', 'a', 'v', 'i', 'o', 4, 'H', 'o', 'l', 'a'};
  fio_str_info_s message = {.data = data, .len = 13};
  print_msg(&message, name, "Sent");

  if (-1 == websocket_write(ws, message, 0)) {
    printf("FAILED TO SEND MESSAGE!");
  }
  time.tv_sec = 10;
  nanosleep(&time, NULL);

  char data3[] = {4, 1, '~', 4, 'a', 'b', 'c', 'd'};
  fio_str_info_s message3 = {.data = data3, .len = 8};
  print_msg(&message3, name, "Sent");

  if (-1 == websocket_write(ws, message3, 0)) {
    printf("FAILED TO SEND MESSAGE!");
  }
  time.tv_sec = 10;
  nanosleep(&time, NULL);

  char data4[] = {4,   6, 'F', 'l', 'a', 'v', 'i',
                  'o', 5, 'A', 'd', 'i', 'o', 's'};
  fio_str_info_s message4 = {.data = data4, .len = 14};
  print_msg(&message4, name, "Sent");

  if (-1 == websocket_write(ws, message4, 0)) {
    printf("FAILED TO SEND MESSAGE!");
  }
  time.tv_sec = 1;
  nanosleep(&time, NULL);

  char data2[] = {5, 6, 'F', 'l', 'a', 'v', 'i', 'o'};
  fio_str_info_s message2 = {.data = data2, .len = 8};
  print_msg(&message2, name, "Sent");

  if (-1 == websocket_write(ws, message2, 0)) {
    printf("FAILED TO SEND MESSAGE!");
  }
  time.tv_sec = 1;
  nanosleep(&time, NULL);

  char data5[] = {5, 1, '~'};
  fio_str_info_s message5 = {.data = data5, .len = 3};
  print_msg(&message5, name, "Sent");

  if (-1 == websocket_write(ws, message5, 0)) {
    printf("FAILED TO SEND MESSAGE!");
  }
  time.tv_sec = 1;
  nanosleep(&time, NULL);
}

int main() {
  char *url = "ws://3.12.154.160:8080/?name=Flavio";
  printf("Connecting to: %s\n", url);
  if (-1 == websocket_connect(url, .on_open = on_open, .on_message = on_message,
                              .on_close = on_close, .udata = "Flavio")) {
    printf("Failed to connect to WebSocket server.\n");
    return 1;
  }

  char *url2 = "ws://3.12.154.160:8080/?name=Jose";
  printf("Connecting to: %s\n", url2);
  if (-1 == websocket_connect(url2, .on_open = on_open_2,
                              .on_message = on_message, .on_close = on_close,
                              .udata = "Jose")) {

    printf("Failed to connect to WebSocket server.\n");
    return 1;
  }

  char *url4 = "ws://3.12.154.160:8080/?name=Zuleny";
  printf("Connecting to: %s\n", url4);
  if (-1 == websocket_connect(url4, .on_open = on_open,
                              .on_message = on_message, .on_close = on_close,
                              .udata = "Zuleny")) {

    printf("Failed to connect to WebSocket server.\n");
    return 1;
  }
  // Start client event loop
  fio_start(.threads = 2);
  // sleep(5);
}
