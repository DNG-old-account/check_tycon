#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include <curl/curl.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

/* Icinga return codes */
#define RETURN_OK       0
#define RETURN_WARNING  1
#define RETURN_CRITICAL 2
#define RETURN_UNKNOWN  3

enum {
  VALUE_OK,
  VALUE_TOO_BIG,
  VALUE_TOO_SMALL
};

struct wbuf {
  int cur;
  int size;
  char *head;
};

struct kvpair {
  char *key;
  float value;
  int status;
};

struct check {
  char key[32];
  float min;
  float max;
  struct check *next;
};

#define N_PAIRS     32

struct kvpair pair[N_PAIRS];
struct check *check_head = NULL, **check_tailp = &check_head;

char username[128] = "root";
char password[128] = "password";
char hostname[256] = "hostname";

void add_check(char *key, float min, float max)
{
  struct check *new_check;

  if (min > max)
    return;

  new_check = malloc(sizeof(struct check));

  strcpy(new_check->key, key);
  new_check->min = min;
  new_check->max = max;

  *check_tailp = new_check;
  check_tailp = &new_check->next;
  new_check->next = NULL;
}

/* 
 * A checkstring is a string of the form "KEY:MIN:MAX"
 * if MIN or MAX are blank they are interpreted as
 * plus or minus infinity.
 */
int add_checkstring(char *str)
{
  char *key, *minstr, *maxstr;
  char *p, *q;
  int count=0;
  float min, max;

  key = str;
  q = str;

  while (*q && *q != ':')
    q++;
  if (*q != ':')
    return -1;
  *q++ = 0;

  minstr = q;
  while (*q && *q != ':')
    q++;
  if (*q != ':')
    return -1;
  *q++ = 0;

  maxstr = q;
  while (*q && *q != ':')
    q++;
  if (*q)
    return -1;

  if (*minstr)
    min = strtof(minstr, NULL);
  else
    min = -INFINITY;

  if (*maxstr)
    max = strtof(maxstr, NULL);
  else
    max = INFINITY;

  add_check(key, min, max);
  return 0;
}

int process_checks(char *str)
{
  char *p, *q;
  char store;

  p = str;
  q = str;

  while (*q) {
    while (*q && *q != ',')
      q++;

    store = *q;
    *q = 0;
    if (add_checkstring(p) < 0)
      return -1;
    p = ++q;

    if (!store)
      break;
  }

  return 0;
}

void dump_checks()
{
  struct check *ch;

  for (ch=check_head;ch;ch=ch->next) {
    printf ("%s %.2f %.2f\n", ch->key, ch->min, ch->max);
  }

}

void free_checks()
{
  struct check *ch;

  ch=check_head;
  while (ch) {
    struct check *p;

    p = ch;
    ch = ch->next;
    free(p);
  }    
}

int wbuf_alloc(struct wbuf *wbuf, int bufsize)
{
  wbuf->head = malloc(bufsize);
  if (!wbuf->head)
    return 0;

  wbuf->size = bufsize;
  wbuf->cur = 0;
  return wbuf->size;
}

void wbuf_free(struct wbuf *wbuf)
{
  free(wbuf->head);
  wbuf->size = 0;
  wbuf->cur = 0;
}

size_t write_callback(char *ptr, size_t size, size_t nmemb, struct wbuf *wbuf)
{
  if (nmemb*size + wbuf->cur < wbuf->size) {
    memcpy(wbuf->head + wbuf->cur, ptr, nmemb*size);
    wbuf->cur += nmemb*size;
    return nmemb*size;
  } else {
    return 0;
  }
}

int get_status(struct wbuf *wbuf)
{
  CURL *curl;
  CURLcode res;
  int ret = 0;
  char url[512];

  sprintf(url, "http://%s/status.xml", hostname);

  curl = curl_easy_init();

  if (curl) {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, wbuf);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERNAME, username);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failure: %s\n", curl_easy_strerror(res));
      ret = -1;
    }
    curl_easy_cleanup(curl);
  } else {
    fprintf(stderr, "unable to init curl\n");
    ret = -1;
  }

  return ret;
}

int parse_status(struct wbuf *wbuf)
{
  xmlDocPtr doc;
  xmlNode *node;
  int idx = 0;

  doc = xmlReadMemory(wbuf->head, wbuf->cur, "status.xml", NULL, 0);
  if (doc == NULL) {
    fprintf(stderr, "Failed to parse\n");
    return -1;
  }

  node = xmlDocGetRootElement(doc); /* <response> */

  for (node=node->children;node;node=node->next) {
    if (strcmp(node->name, "text") == 0)
      continue;

    pair[idx].key = strdup(node->name);
    pair[idx].value = strtof(node->children->content, NULL);
    pair[idx].status = VALUE_OK;
    idx++;
  }
  xmlFreeDoc(doc);

  return 0;
}

void init_pairs()
{
  int i;

  for (i=0;i<N_PAIRS;i++) {
    pair[i].key = 0;
  }
}


int perform_checks(struct kvpair *p)
{
  struct check *ch;
  int retcode = 0;

  for (ch=check_head;ch;ch=ch->next) {
    if (strcmp(p->key, ch->key) != 0)
      continue;

    if (p->value < ch->min) {
      p->status = VALUE_TOO_SMALL;
      retcode = -1;
    } else if (p->value > ch->max) {
      p->status = VALUE_TOO_BIG;
      retcode = -1;
    } else {
      p->status = VALUE_OK;
      retcode = 0;
    }
  }

  return retcode;
}

void print_status(int retcode)
{
  if (retcode == 0)
    printf ("TYCON OK");
  else
    printf ("TYCON PROBLEM");
  
  if (pair[0].key) {
    int i = 0;
    while (pair[i].key) {
      printf (" %s=%.1f", pair[i].key, pair[i].value);
      if (pair[i].status != VALUE_OK)
	printf ("*");
      i++;
    }
  }

  printf("\n");
}

main(int argc, char **argv)
{
  struct wbuf wbuf;
  struct check *ch;
  int opt, ret, i;
  int retcode;

  while ((opt = getopt(argc, argv, "h:u:p:c:")) != -1) {
    switch(opt) {
    case 'h':
      strcpy(hostname, optarg);
      break;
    case 'u':
      strcpy(username, optarg);
      break;
    case 'p':
      strcpy(password, optarg);
      break;
    case 'c':
      process_checks(optarg);
      break;
    default:
      exit(RETURN_UNKNOWN);
    }
  }

  init_pairs();
  wbuf_alloc(&wbuf, 2048);

  curl_global_init(CURL_GLOBAL_ALL);
  ret = get_status(&wbuf);
  curl_global_cleanup();

  if (ret < 0) {
    retcode = RETURN_WARNING;
    goto out;
  }

  if (parse_status(&wbuf) < 0) {
    retcode = RETURN_WARNING;
    goto out;
  }

  wbuf_free(&wbuf);

  retcode = RETURN_OK;

  for (i=0;i<N_PAIRS;i++) {
    if (!pair[i].key)
      break;

    if (perform_checks(&pair[i]) < 0)
      retcode = RETURN_CRITICAL;
  }

 out:
  free_checks();
  print_status(retcode);
  exit(retcode);
}
