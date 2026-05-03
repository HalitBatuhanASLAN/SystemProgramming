#include "order.h"

#include <ctype.h>
#include <string.h>

int priority_from_text(const char *text, priority_t *priority)
{
    if(strcmp(text, "EXPRESS") == 0)
    {
        *priority = PRIORITY_EXPRESS;
        return 1;
    }

    if(strcmp(text, "STANDARD") == 0)
    {
        *priority = PRIORITY_STANDARD;
        return 1;
    }

    if(strcmp(text, "ECONOMY") == 0)
    {
        *priority = PRIORITY_ECONOMY;
        return 1;
    }

    return 0;
}

const char *priority_to_text(priority_t priority)
{
    if(priority == PRIORITY_EXPRESS)
    {
        return "EXPRESS";
    }

    if(priority == PRIORITY_STANDARD)
    {
        return "STANDARD";
    }

    return "ECONOMY";
}

int order_has_valid_recipient(const char *recipient)
{
    size_t index;
    size_t length = strlen(recipient);

    if(length == 0 || length > MAX_RECIPIENT_LENGTH)
    {
        return 0;
    }

    for(index = 0; index < length; index++)
    {
        unsigned char ch = (unsigned char)recipient[index];

        if(!isalnum(ch) && ch != '_')
        {
            return 0;
        }
    }

    return 1;
}

int order_compare(const order_t *left, const order_t *right)
{
    if(left->priority < right->priority)
    {
        return -1;
    }

    if(left->priority > right->priority)
    {
        return 1;
    }

    if(left->id < right->id)
    {
        return -1;
    }

    if(left->id > right->id)
    {
        return 1;
    }

    return 0;
}
