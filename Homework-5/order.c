#include "order.h"

#include <string.h>

static int is_ascii_name_char(unsigned char ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_';
}

/*
 * Converts the priority word read from the input file into the enum value.
 * The parser calls this function for every valid-looking line. If the text
 * is not one of the homework keywords, the line is treated as invalid.
 */
int priority_from_text(const char *text, priority_t *priority)
{
    /* Convert file text to the enum value used by heap. */
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

/*
 * Converts an enum priority back to the exact text used in output lines.
 * This keeps all logging functions using the same spelling for priorities.
 */
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

/*
 * Checks the recipient field from the input line. The teacher clarified that
 * names may contain only ASCII letters, digits, and underscores. This avoids
 * locale dependent behavior and rejects non-ASCII UTF-8 bytes.
 */
int order_has_valid_recipient(const char *recipient)
{
    size_t index;
    size_t length = strlen(recipient);

    /* Recipient has no spaces and at most 32 chars. */
    if(length == 0 || length > MAX_RECIPIENT_LENGTH)
    {
        return 0;
    }

    for(index = 0; index < length; index++)
    {
        unsigned char ch = (unsigned char)recipient[index];

        if(!is_ascii_name_char(ch))
        {
            return 0;
        }
    }

    return 1;
}

/*
 * Comparison used by the heap. An order is smaller if it has higher priority.
 * If priorities are the same, lower id wins. This is the main rule that makes
 * the scheduling deterministic.
 */
int order_compare(const order_t *left, const order_t *right)
{
    /* Lower enum value means higher delivery priority. */
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

/*
 * Converts simulation units to real milliseconds. The updated homework says
 * one unit is 500 ms, so all sleeps and statistics use this same helper.
 */
long order_duration_ms(const order_t *order)
{
    return (long)order->duration_units * SIMULATION_UNIT_MS;
}
