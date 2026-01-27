cmake_minimum_required(VERSION 3.28)

project(ztl)

target_sources(app PRIVATE digital_output.c digital_input.c sensor.c nvs.c bt.c common_workqueue.c init.c)
