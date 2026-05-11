// takes data from api sends to json, parses and sends furthermore

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "../headers/seismic.h"
#include "../headers/meteorological.h"
#include "../headers/hydro.h"


// buffer HTTP
struct Memory {
    char *data;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct Memory *mem = (struct Memory *)userp;

    mem->data = realloc(mem->data, mem->size + realsize + 1);
    if (!mem->data) return 0;

    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

// generic GET request
char *http_get(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL; // Verificăm curl prima dată

    struct Memory chunk;
    chunk.data = malloc(1);
    chunk.size = 0;

    // Verificăm imediat dacă malloc a reușit
    if (chunk.data == NULL) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
    fprintf(stderr, "curl error: %s\n", curl_easy_strerror(res));
    free(chunk.data);
    curl_easy_cleanup(curl);
    return NULL;
}
    curl_easy_cleanup(curl);
    return chunk.data;
}
