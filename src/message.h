#ifndef MESSAGE_H
#define MESSAGE_H

void handle_message(FILE *rsp, char *message, size_t message_length);
bool message_loop_begin(char *socket_path);
bool message_loop_end(void);

#endif
