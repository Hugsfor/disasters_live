#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "../headers/seismic.h"
#include "../headers/meteorological.h"
#include "../headers/hydro.h"

struct Memory {
    char  *data;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct Memory *mem = (struct Memory *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

// Generic GET request — with proper headers so APIs don't block us.
char *http_get(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    struct Memory chunk;
    chunk.data = malloc(1);
    chunk.size = 0;
    if (!chunk.data) { curl_easy_cleanup(curl); return NULL; }

    // Headers that make servers treat us like a real client
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json, application/xml, text/xml, text/plain, */*");
    headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
    headers = curl_slist_append(headers, "Cache-Control: no-cache");

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/120.0.0.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);   // follow redirects
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);  // 15s total timeout
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);  // 10s connect timeout
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl error [%s]: %s\n", url, curl_easy_strerror(res));
        free(chunk.data);
        return NULL;
    }

    // Check for HTML error page — if the response starts with <!DOCTYPE or <html
    // the server returned an error page instead of data, treat as failure
    if (chunk.size > 9 &&
        (strncasecmp(chunk.data, "<!doctype", 9) == 0 ||
         strncasecmp(chunk.data, "<html",     5) == 0)) {
        fprintf(stderr, "http_get: got HTML error page from %s\n", url);
        free(chunk.data);
        return NULL;
    }

    return chunk.data;
}
