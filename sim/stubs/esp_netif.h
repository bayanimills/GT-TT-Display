#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"
typedef struct esp_netif_obj esp_netif_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip, netmask, gw; } esp_netif_ip_info_t;
typedef struct { esp_netif_ip_info_t ip_info; } ip_event_got_ip_t;
#define IP_EVENT       ((esp_event_base_t)"IP_EVENT")
#define IP_EVENT_STA_GOT_IP 0
#define IP2STR(a) (uint8_t)((a)->addr & 0xff), (uint8_t)(((a)->addr >> 8) & 0xff), (uint8_t)(((a)->addr >> 16) & 0xff), (uint8_t)(((a)->addr >> 24) & 0xff)
#define IPSTR "%d.%d.%d.%d"
/* Not inline and not a no-op: sim_rt.c records that this was called, because
 * the TCP/IP stack being up is a precondition the firmware can get wrong and
 * a simulator that always says yes cannot catch it. */
esp_err_t esp_netif_init(void);
static inline esp_netif_t *esp_netif_create_default_wifi_sta(void) { return (esp_netif_t *)1; }
static inline esp_err_t esp_netif_get_ip_info(esp_netif_t *n, esp_netif_ip_info_t *i)
{ (void)n; if (i) { i->ip.addr = 0x7200A8C0; i->gw.addr = 0x0100A8C0; i->netmask.addr = 0x00FFFFFF; } return ESP_OK; }
static inline esp_netif_t *esp_netif_get_handle_from_ifkey(const char *k) { (void)k; return (esp_netif_t *)1; }
