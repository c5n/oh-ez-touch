/**
 * @file linux/port_net.c
 *
 * What the host's network is doing, read from the host: the interface that
 * carries the default route, its addresses, and the first nameserver in
 * /etc/resolv.conf.
 *
 * There is no scan, and no radio to scan with. The canned list below is in the
 * spirit of sim/sitemap_fixture.cpp -- it exists so the WLAN settings tab can
 * be laid out and clicked through -- and it is here rather than in
 * ui_settings.cpp so that the tab itself needs no target guard.
 */
#include "port_net.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void copy_in_addr(char *dst, size_t dst_size, const struct sockaddr *sa)
{
    if (sa == NULL || sa->sa_family != AF_INET)
        return;

    const struct sockaddr_in *in = (const struct sockaddr_in *)sa;

    inet_ntop(AF_INET, &in->sin_addr, dst, (socklen_t)dst_size);
}

/* The interface with the default route, from the kernel's own table. The
 * destination of the default route is 00000000, and the fields are the
 * hexadecimal little-endian addresses procfs has always printed. */
static bool default_route(char *interface, size_t interface_size, char *gateway,
                          size_t gateway_size)
{
    FILE *f = fopen("/proc/net/route", "r");

    if (f == NULL)
        return false;

    char line[256];
    bool found = false;

    /* Skip the header. */
    if (fgets(line, sizeof(line), f) != NULL)
    {
        while (fgets(line, sizeof(line), f) != NULL)
        {
            char name[IF_NAMESIZE + 1] = "";
            unsigned long destination = 0;
            unsigned long next_hop = 0;

            if (sscanf(line, "%16s %lx %lx", name, &destination, &next_hop) != 3)
                continue;

            if (destination != 0)
                continue;

            struct in_addr addr = { .s_addr = (in_addr_t)next_hop };

            snprintf(interface, interface_size, "%s", name);
            inet_ntop(AF_INET, &addr, gateway, (socklen_t)gateway_size);
            found = true;
            break;
        }
    }

    fclose(f);

    return found;
}

static void first_nameserver(char *dst, size_t dst_size)
{
    FILE *f = fopen("/etc/resolv.conf", "r");

    if (f == NULL)
        return;

    char line[256];

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char address[64] = "";

        if (sscanf(line, "nameserver %63s", address) == 1)
        {
            snprintf(dst, dst_size, "%s", address);
            break;
        }
    }

    fclose(f);
}

void port_net_info(port_net_info_t *out)
{
    memset(out, 0, sizeof(*out));

    /* No radio, and saying so honestly: a percentage here would be invented. */
    out->rssi = PORT_NET_RSSI_WIRED;

    gethostname(out->hostname, sizeof(out->hostname) - 1);

    char interface[IF_NAMESIZE + 1] = "";

    out->connected = default_route(interface, sizeof(interface), out->gateway,
                                   sizeof(out->gateway));

    /* The SSID field is the interface name here. It is what the Info tab's
     * "which network am I on" row is for, and on a wired host that is the
     * answer to the same question. */
    snprintf(out->ssid, sizeof(out->ssid), "%s", interface);

    first_nameserver(out->dns, sizeof(out->dns));

    struct ifaddrs *list = NULL;

    if (getifaddrs(&list) != 0)
        return;

    for (struct ifaddrs *ifa = list; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_name == NULL || strcmp(ifa->ifa_name, interface) != 0)
            continue;

        if (ifa->ifa_addr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            copy_in_addr(out->ip, sizeof(out->ip), ifa->ifa_addr);
            copy_in_addr(out->netmask, sizeof(out->netmask), ifa->ifa_netmask);
        }
        else if (ifa->ifa_addr->sa_family == AF_PACKET)
        {
            /* sockaddr_ll without dragging in <linux/if_packet.h>: the hardware
             * address sits at a fixed offset that has not moved in the lifetime
             * of the ABI. */
            const unsigned char *raw = (const unsigned char *)ifa->ifa_addr;
            const unsigned char *mac = raw + 12;

            snprintf(out->mac, sizeof(out->mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            snprintf(out->bssid, sizeof(out->bssid), "%s", out->mac);
        }
    }

    freeifaddrs(list);
}

static const port_net_ap_t canned[] = {
    { "FRITZ!Box 7590",                -42, true  },
    { "oheztouch-lab",                 -55, true  },
    { "Nachbar-WLAN",                  -71, true  },
    { "Gastnetz",                      -78, false },
    { "a-very-long-network-name-here", -88, true  },
};

#define CANNED_COUNT ((int)(sizeof(canned) / sizeof(canned[0])))

bool port_net_scan_start(void)
{
    return true;
}

int port_net_scan_poll(void)
{
    /* Answered at once rather than after a plausible delay: pretending to take
     * two seconds would only make the tab slower to work on. */
    return CANNED_COUNT;
}

bool port_net_scan_result(int index, port_net_ap_t *out)
{
    if (index < 0 || index >= CANNED_COUNT)
        return false;

    *out = canned[index];

    return true;
}

void port_net_scan_free(void)
{
}
