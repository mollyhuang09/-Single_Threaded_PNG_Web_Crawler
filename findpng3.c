
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>

#define PNG_SIG_SIZE    8
#define MAX_WAIT_MSECS 30*1000 /* Wait max. 30 seconds */
#define MAX_URL_LEN 2048
#define MAX_PNG_URLS 50
#define SEED_URL "http://ece252-1.uwaterloo.ca/lab4/"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */
#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9
#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

static const char *urls[] = {
  "http://www.microsoft.com",
  "http://www.yahoo.com",
  "http://www.wikipedia.org",
  "http://slashdot.org"
};
#define CNT 4

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct {
    char **urls;
    int count;
    int capacity;
    int size;
    int checked;
} UrlList;

UrlList *create_url_list(int capacity)
{
    UrlList *list = malloc(sizeof(UrlList));
    list->urls = malloc(capacity * sizeof(char *));
    list->count = 0;
    list->capacity = capacity;
    for (int i = 0; i < capacity; i++){
        list->urls[i] = NULL;
    }
    list->size = 0;
    list->checked = 0;
    return list;
}

int url_exists(UrlList *list, const char *url) {
    for (int i = 0; i < list->count; ++i) {
        if (strcmp(list->urls[i], url) == 0) {
            return 1;
        }
    }
    return 0;
}

void add_url(UrlList *list, const char *url)
{
    if (list->count < list->capacity) {
        list->urls[list->count++] = strdup(url);
        list->size++;
    }
}

void free_url_list(UrlList *list) {
    if (list == NULL) return;
    for (int i = 0; i < list->count; ++i) {
        free(list->urls[i]);
    }
    free(list->urls);
    free(list);
}

// bool is_png(char *buf){
//     char png_header[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

//     for (int i = 0; i < 8; i ++){
//         if ( !(png_header[i] == buf[i]) ){
//             return false;
//         }
//     }
    
//     return true;
// };

int is_png(unsigned char *buf, size_t n){
    if(n < PNG_SIG_SIZE){
        return 0;
    }
    return !memcmp(buf, "\x89PNG\r\n\x1a\n", PNG_SIG_SIZE);
}

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

// #ifdef DEBUG1_
//     printf("%s", p_recv);
// #endif /* DEBUG1_ */
    if (realsize > strlen(ECE252_HEADER) &&
	strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

    /* extract img sequence number */
	p->seq = atoi(p_recv + strlen(ECE252_HEADER));

    }
    return realsize;
}


size_t write_cb_curl(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);   
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}

static void init(CURLM *cm, char *url, RECV_BUF *ptr)
{
    CURL *eh = curl_easy_init();
    curl_easy_setopt(eh, CURLOPT_URL, url);
    curl_easy_setopt(eh, CURLOPT_PRIVATE, (void *)ptr);
    curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_cb_curl); 
    curl_easy_setopt(eh, CURLOPT_WRITEDATA, (void *)ptr);
    curl_easy_setopt(eh, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    curl_easy_setopt(eh, CURLOPT_HEADERDATA, (void *)ptr);
    curl_easy_setopt(eh, CURLOPT_USERAGENT, "ece252 lab4 crawler");
    curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(eh, CURLOPT_UNRESTRICTED_AUTH, 1L);
    curl_easy_setopt(eh, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(eh, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(eh, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(eh, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    curl_easy_setopt(eh, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_multi_add_handle(cm, eh);
}

int recv_buf_init(RECV_BUF *ptr, size_t max_size) {
    if (ptr == NULL) {
        return 1;
    }

    ptr->buf = malloc(max_size);
    if (ptr->buf == NULL) {
        return 2;
    }

    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1; // valid seq should be positive
    return 0;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath)
{
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        //printf("No result\n");
        return NULL;
    }
    return result;
}

htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);
    
    if ( doc == NULL ) {
        //fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url, UrlList *frontier)
{
    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;
    if (buf == NULL) {
        return 1;
    }
    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i=0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if ( follow_relative_links ) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if ( href != NULL && !strncmp((const char *)href, "http", 4) ) {
                //printf("href: %s\n", href);
                add_url(frontier, href);
            }
            xmlFree(href);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    return 0;
}

void recv_buf_cleanup(RECV_BUF *ptr) {
    if (ptr->buf) {
        free(ptr->buf);
    }
    ptr->size = 0;
    ptr->max_size = 0;
    ptr->seq = -1;
}

void process_html(CURL *curl_handle, RECV_BUF *p_recv_buf, UrlList *frontier)
{
    char fname[256];
    int follow_relative_link = 1;
    char *url = NULL; 
    pid_t pid = getpid();
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url, frontier); 
    // sprintf(fname, "./output_%d.html", pid);
    // return write_file(fname, p_recv_buf->buf, p_recv_buf->size);
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf, UrlList *png_urls, int max_pngs)
{
    pid_t pid = getpid();
    char fname[256];
    char *eurl = NULL; /* effective URL */
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if ((eurl != NULL) && (is_png(p_recv_buf->buf, p_recv_buf->size))) {
        if(png_urls->count >= max_pngs) {
            return 0;
        }
        add_url(png_urls, eurl);
    }
}

void process_data(CURL *curl_handle, RECV_BUF *p_recv_buf, UrlList *frontier, UrlList *visited, UrlList *png_urls, int max_pngs)
{
    CURLcode res;
    char fname[256];
    pid_t pid = getpid();
    long response_code;
    res = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    // if (res == CURLE_OK) {
    //     printf("Response code: %ld\n", response_code);
    // }
    if (response_code >= 400) { 
        //fprintf(stderr, "Error.\n");
        return;
    }
    char *ct = NULL;
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if (res == CURLE_OK && ct != NULL) {
        //printf("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    } else {
        fprintf(stderr, "Failed to obtain Content-Type\n");
        return;
    }
    if (strstr(ct, CT_HTML)) {
        process_html(curl_handle, p_recv_buf, frontier);
    } else if (strstr(ct, CT_PNG)) {
        process_png(curl_handle, p_recv_buf, png_urls, max_pngs);
    } else {
        sprintf(fname, "./output_%d", pid);
    }
    //return write
}

int main(int argc, char *argv[])
{
    char seed_url[MAX_URL_LEN] = "http://ece252-1.uwaterloo.ca/lab4";
    int number_connection = 1;
    int max_pngs = MAX_PNG_URLS;
    FILE *logfile = NULL;
    UrlList *frontier = create_url_list(3000);
    UrlList *visited = create_url_list(3000);
    UrlList *png_urls = create_url_list(max_pngs);

    // Parse in arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            number_connection = atoi(argv[i+1]);
            i++;
        } else if (strcmp(argv[i], "-m") == 0) {
            max_pngs = atoi(argv[i+1]);
            if (max_pngs > 50){
                max_pngs = 50;
            }
            i++;
        } else if (strcmp(argv[i], "-v") == 0) {
            logfile = fopen(argv[i+1], "w");
            i++;
        } else {
            strcpy(seed_url, argv[i]);
        }
    }

    // CURL initialization
    CURLM *cm=NULL;
    CURL *eh=NULL;
    CURLMsg *msg=NULL;
    CURLcode return_code=0;
    int still_running=0, i=0, msgs_left=0;
    int http_status_code;
    char *szUrl;
    char *content_type;
    curl_global_init(CURL_GLOBAL_ALL);
    cm = curl_multi_init();
    RECV_BUF *recv_buf[number_connection];

    curl_global_init(CURL_GLOBAL_ALL);

    cm = curl_multi_init();

    add_url(frontier, seed_url);
    int j = 0;
    //while ( j < 10 ) {
    while (png_urls->count < max_pngs) {
        for (i = 0; i < number_connection; ++i) {
            RECV_BUF *ptr = malloc(sizeof(RECV_BUF));
            recv_buf_init(ptr, BUF_SIZE);
            char *url[MAX_URL_LEN];
            // get an url
            if(frontier->count > 0) {
                strcpy(url, frontier->urls[--frontier->count]);
                printf("get url: %s\n", url);
                if(url != NULL && !url_exists(visited, url)) {
                    init(cm, url, ptr);
                }
            }
        }
        curl_multi_perform(cm, &still_running);
        do {
            int numfds=0;
            int res = curl_multi_wait(cm, NULL, 0, MAX_WAIT_MSECS, &numfds);
            if(res != CURLM_OK) {
                fprintf(stderr, "error: curl_multi_wait() returned %d\n", res);
                return EXIT_FAILURE;
            }
            curl_multi_perform(cm, &still_running);
        } while(still_running);

        // int count = 0;
        while ((msg = curl_multi_info_read(cm, &msgs_left))) {
            // printf("%d\n", count);
            if (msg->msg == CURLMSG_DONE) {
                eh = msg->easy_handle;

                return_code = msg->data.result;
                if(return_code!=CURLE_OK) {
                    //fprintf(stderr, "CURL error code: %d\n", msg->data.result);
                    continue;
                }

                // Get HTTP status code
                http_status_code=0;
                szUrl = NULL;
                RECV_BUF *recv;
                //curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &http_status_code);
                curl_easy_getinfo(eh, CURLINFO_PRIVATE, &recv);
                //curl_easy_getinfo(eh, CURLINFO_CONTENT_TYPE, &content_type);

                // recv = recv_buf[count];
                //curl_easy_getinfo(eh, CURLINFO_EFFECTIVE_URL, &szUrl);
                //printf("szUrl: %s\n", szUrl);

                // if(http_status_code==200) {
                //     //printf("200 OK for %s\n", szUrl);
                // } else {
                //     //fprintf(stderr, "GET of %s returned http status code %d\n", szUrl, http_status_code);
                // }
                //printf("%s\n", recv->buf);
                process_data(eh, recv, frontier, visited, png_urls, max_pngs);

                // if(strstr(content_type, CT_HTML)) {
                //     printf("HTML: %s\n", szUrl);
                //     find_http(recv->buf, recv->size, 1, szUrl, frontier);
                // }else if (strstr(content_type, CT_PNG)){
                //     if(is_png(recv->buf, recv->size)) {
                //         printf("PNG: %s\n", szUrl);
                //         add_url(png_urls, szUrl);
                //     }
                // }
                // add_url(visited, szUrl);
                recv_buf_cleanup(recv);
                curl_multi_remove_handle(cm, eh);
                curl_easy_cleanup(eh);
            }
            else {
                fprintf(stderr, "error: after curl_multi_info_read(), CURLMsg=%d\n", msg->msg);
            }
            //count ++;
        }
        j++;
        // printf("here\n");
    }
    curl_multi_cleanup(cm);

    if (logfile != NULL) {
        printf("%d\n", visited->count);
        for (int i = 0; i < visited->count; ++i) {
            fprintf(logfile, "%s\n", visited->urls[i]);
        }
        fclose(logfile);
    }

    FILE *pngurl = fopen("png_urls.txt", "w");
    if (pngurl) {
        for (int i = 0; i < png_urls->count; ++i) {
            fprintf(pngurl, "%s\n", png_urls->urls[i]);
        }
        fclose(pngurl);
    }

    return 0;

    return EXIT_SUCCESS;
}
