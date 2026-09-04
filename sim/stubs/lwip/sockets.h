#pragma once
/* On the panel these come from lwIP; on a host they are the real thing. So the
 * simulator measures actual pool latency rather than pretending to, which is
 * the whole point of being able to test this without hardware. */
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
