#pragma once
#include <ctime>

extern volatile time_t time_of_create_event;

void watch_dev_input();

bool have_new_device();
