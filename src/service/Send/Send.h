#pragma once

//
void send_loop();

//
typedef void (*SendFunc)(int);
void send_register(SendFunc send_func);

//
void send_start();

// 0 - 100 of the current send
int send_progress();
void send_progress_reset();
