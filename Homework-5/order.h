#ifndef ORDER_H
#define ORDER_H

#define MAX_RECIPIENT_LENGTH 32

typedef enum priority
{
    PRIORITY_EXPRESS = 1,
    PRIORITY_STANDARD = 2,
    PRIORITY_ECONOMY = 3
} priority_t;

typedef struct order
{
    int id;
    char recipient[MAX_RECIPIENT_LENGTH + 1];
    priority_t priority;
    int duration_units;
} order_t;

int priority_from_text(const char *text, priority_t *priority);
const char *priority_to_text(priority_t priority);
int order_has_valid_recipient(const char *recipient);
int order_compare(const order_t *left, const order_t *right);

#endif
