#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/http_client.h"
#include "cJSON.h"

#define SSID "YOUR_SSID"
#define PWD "YOUR_PASSWORD"
#define AUTH CYW43_AUTH_WPA2_MIXED_PSK
#define COUNTRY CYW43_COUNTRY_FINLAND

bool connect_wifi(void);
err_t get_body(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err);
void check_connection_result(
    void *arg, 
    httpc_result_t httpc_result, 
    uint32_t rx_content_len, 
    uint32_t srv_res, 
    err_t err
);
void get_data(void);
void parse_data(char *data);

int main(void) {    

    stdio_init_all();

    if (!connect_wifi()) {
        printf("Wifi connection failed");
        return 1;
    }

    while(1) {

        get_data();
        sleep_ms(10000);
    }

    return 0;
}

bool connect_wifi(void) {

    // Initialize the CYW43 architecture for use in a specific country.
    if (cyw43_arch_init_with_country(COUNTRY)) {
        printf("Wi-Fi init failed\n");
        return false;
    }

    // Enables the Wifi in Station mode to connect to AP.
    cyw43_arch_enable_sta_mode();

    // Attempt to connect to a wireless access point.
    if (cyw43_arch_wifi_connect_blocking(SSID, PWD, AUTH)) {
        printf("Wifi failed to connect.\n");
        return false;
    }

    // Check for correct link status.
    for (uint8_t i = 0; i < 10; i++) {

        int link_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (link_status == CYW43_LINK_UP) {
            // Read the ip address in a human readable way
            uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
            printf("Connected.\n IP address %d.%d.%d.%d\n", 
                    ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
            return true;
        }
        sleep_ms(1000);
    }
    printf("Wifi ip address not found\n");
    return false;
}

void get_data(void) {

    char site[] = "sahkohinta-api.fi";
    char uri[] = "/api/v1/halpa?tunnit=24&tulos=sarja";
    uint16_t port = 80;  //80 for http.

    httpc_connection_t settings;  //proxy settings not used
    settings.result_fn = check_connection_result;  //callback
    settings.headers_done_fn = ERR_OK;
    /* headers_done_fn should use another callback function, but it's not in use currently.
     * Instead this program uses recv_fn, eg. html body.
     * Either way this is necessary settings parameter so just give it it's return value ERR_OK.
     */

    err_t err = httpc_get_file_dns(
        site,
        port,
        uri,
        &settings,
        get_body,  //recv_fn, eg. scraped data. callback
        NULL,
        NULL
    );
}

void parse_data(char *data) {

    /* Used to parse the arrays that gives. 
     * Works with format [{"asdf":"1234","abcd":"0987"},{another},{another}]
     */

    // TODO: save data to global variable.
    float price[24];
    cJSON *json = cJSON_Parse(data); 
  
    // Access the JSON data 
    uint32_t array_size = cJSON_GetArraySize(json);
    for (uint16_t i = 0; i < array_size; i++) {

        cJSON *element = cJSON_GetArrayItem(json, i);
        cJSON *hinta = cJSON_GetObjectItem(element, "hinta");
        price[i] = atof(hinta->valuestring);
        printf("%f\n", price[i]);
    }
  
    // Delete the JSON object 
    cJSON_Delete(json); 
}

err_t get_body(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err) {

    /* Gets called for every received payload. Is used to first parse individual packets and 
     * then used in conjugtion with parse_data() to get the useful info off the html packets.
     * It's possible and simpler to just use pbuf_copy_partial() to save packets to one big buffer,
     * but this method saves memory when acessing large files.
     */

    static uint32_t cutoff = 0;
    uint32_t index1, index2;
    uint32_t previous_splice = cutoff;
    static char *splice = NULL;
    char *data = p->payload;
    bool found1 = false, found2 = false, end = false; 

    /* Find indexes for parsing. 
     * TODO: optimize and check for errors
     */
    for (uint32_t i = 0; i <= p->len; i++) {

        if (!found1 && data[i] == '{') {
            index1 = i;
            found1 = true;
        }
        if (!found2 && data[p->len - i - 1] == '}') {
            index2 = p->len - i;
            cutoff = i;
            found2 = true;
        }
        else if (data[p->len - i - 1] == ']') {
            end = true;
        }
        if (found1 && found2) {
            break;
        }
    }

    // Format for parsing
    char *buffer = malloc(previous_splice + index2 + 3);  //buffer + '[' + ']' + '\0'
    if (buffer == NULL) {
        printf("Memory error\n");
        return ERR_MEM;
    }
    buffer[0] = '[';

    if (previous_splice != 0) {
        memcpy(buffer + 1, splice, previous_splice);
        memcpy(buffer + previous_splice, data, index2);
        buffer[previous_splice + index2] = ']';
        buffer[previous_splice + index2 + 1] = '\0';
    }
    else {
        memcpy(buffer + 1, data + index1, index2 - index1);
        buffer[index2 - index1 + 1] = ']';
        buffer[index2 - index1 + 2] = '\0';
    }

    if (end) {
        free(splice);
        splice = NULL;
        cutoff = 0;
    }
    else {
        splice = malloc(cutoff + 1);
        if (splice == NULL) {
            printf("Memory error\n");
            return ERR_MEM;
        }
        memcpy(splice, data + p->len - cutoff + 1, cutoff);
        splice[cutoff + 1] = '\0';
    }

    parse_data(buffer);
    free(buffer);

    return ERR_OK;
}

void check_connection_result(
    void *arg, 
    httpc_result_t httpc_result, 
    uint32_t rx_content_len, 
    uint32_t srv_res, 
    err_t err
) {
    /* Checks that the received data is ok. Might not be useful at all. Not currently in use.
     * TODO: make this useful.
     */

    //srv_res can be omitted.
    if (httpc_result != HTTPC_RESULT_OK && srv_res != 200) {
        //htmlerror = true;
        return;
    }
    //htmlerror = false;
}
