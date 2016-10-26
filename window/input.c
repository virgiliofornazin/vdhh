#include "nqdev.h"
#include "sysemu.h"
#include "util/qapi-types.h"
#include "qmp-commands.h"
#include "ui/input.h"
#include "ui/console.h"

struct QemuInputHandlerState {
    DeviceState       *dev;
    QemuInputHandler  *handler;
    int               id;
    int               events;
    QemuConsole       *con;
    QTAILQ_ENTRY(QemuInputHandlerState) node;
};

typedef struct QemuInputEventQueue QemuInputEventQueue;
struct QemuInputEventQueue {
    enum {
        QEMU_INPUT_QUEUE_DELAY = 1,
        QEMU_INPUT_QUEUE_EVENT,
        QEMU_INPUT_QUEUE_SYNC,
    } type;
    QEMUTimer *timer;
    uint32_t delay_ms;
    QemuConsole *src;
    InputEvent *evt;
    QTAILQ_ENTRY(QemuInputEventQueue) node;
};

static QTAILQ_HEAD(, QemuInputHandlerState) handlers =
    QTAILQ_HEAD_INITIALIZER(handlers);
static VeertuNotifiers mouse_mode_notifiers =
    VEERTU_NOTIFIERS_INIT(mouse_mode_notifiers);

static QTAILQ_HEAD(QemuInputEventQueueHead, QemuInputEventQueue) kbd_queue =
    QTAILQ_HEAD_INITIALIZER(kbd_queue);
static QEMUTimer *kbd_timer;
static uint32_t kbd_default_delay_ms = 10;

QemuInputHandlerState *vmx_input_handler_register(DeviceState *dev,
                                                   QemuInputHandler *handler)
{
    QemuInputHandlerState *s = g_new0(QemuInputHandlerState, 1);
    static int id = 1;

    s->dev = dev;
    s->handler = handler;
    s->id = id++;
    QTAILQ_INSERT_TAIL(&handlers, s, node);

    vmx_input_check_mode_change();
    return s;
}

void vmx_input_handler_activate(QemuInputHandlerState *s)
{
    QTAILQ_REMOVE(&handlers, s, node);
    QTAILQ_INSERT_HEAD(&handlers, s, node);
    vmx_input_check_mode_change();
}

void vmx_input_handler_deactivate(QemuInputHandlerState *s)
{
    QTAILQ_REMOVE(&handlers, s, node);
    QTAILQ_INSERT_TAIL(&handlers, s, node);
    vmx_input_check_mode_change();
}

void vmx_input_handler_unregister(QemuInputHandlerState *s)
{
    QTAILQ_REMOVE(&handlers, s, node);
    g_free(s);
    vmx_input_check_mode_change();
}

void vmx_input_handler_bind(QemuInputHandlerState *s,
                             const char *device_id, int head,
                             Error **errp)
{
    DeviceState *dev;
    QemuConsole *con;

    dev = qdev_find_recursive(sysbus_get_default(), device_id);
    if (dev == NULL) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, device_id);
        return;
    }

    con = vmx_console_lookup_by_device(dev, head);
    if (con == NULL) {
        error_setg(errp, "Device %s is not bound to a Console", device_id);
        return;
    }

    s->con = con;
}

static QemuInputHandlerState*
vmx_input_find_handler(uint32_t mask, QemuConsole *con)
{
    QemuInputHandlerState *s;

    QTAILQ_FOREACH(s, &handlers, node) {
        if (s->con == NULL || s->con != con) {
            continue;
        }
        if (mask & s->handler->mask) {
            return s;
        }
    }

    QTAILQ_FOREACH(s, &handlers, node) {
        if (s->con != NULL) {
            continue;
        }
        if (mask & s->handler->mask) {
            return s;
        }
    }
    return NULL;
}

void qmp_x_input_send_event(bool has_console, int64_t console,
                            InputEventList *events, Error **errp)
{
    InputEventList *e;
    QemuConsole *con;

    con = NULL;
    if (has_console) {
        con = vmx_console_lookup_by_index(console);
        if (!con) {
            error_setg(errp, "console %" PRId64 " not found", console);
            return;
        }
    }

    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        error_setg(errp, "VM not running");
        return;
    }

    for (e = events; e != NULL; e = e->next) {
        InputEvent *event = e->value;

        if (!vmx_input_find_handler(1 << event->kind, con)) {
            error_setg(errp, "Input handler not found for "
                             "event type %s",
                            InputEventKind_lookup[event->kind]);
            return;
        }
    }

    for (e = events; e != NULL; e = e->next) {
        InputEvent *event = e->value;

        vmx_input_event_send(con, event);
    }

    vmx_input_event_sync();
}

static void vmx_input_transform_abs_rotate(InputEvent *evt)
{
    switch (graphic_rotate) {
    case 90:
        if (evt->abs->axis == INPUT_AXIS_X) {
            evt->abs->axis = INPUT_AXIS_Y;
        } else if (evt->abs->axis == INPUT_AXIS_Y) {
            evt->abs->axis = INPUT_AXIS_X;
            evt->abs->value = INPUT_EVENT_ABS_SIZE - 1 - evt->abs->value;
        }
        break;
    case 180:
        evt->abs->value = INPUT_EVENT_ABS_SIZE - 1 - evt->abs->value;
        break;
    case 270:
        if (evt->abs->axis == INPUT_AXIS_X) {
            evt->abs->axis = INPUT_AXIS_Y;
            evt->abs->value = INPUT_EVENT_ABS_SIZE - 1 - evt->abs->value;
        } else if (evt->abs->axis == INPUT_AXIS_Y) {
            evt->abs->axis = INPUT_AXIS_X;
        }
        break;
    }
}

static void vmx_input_event_trace(QemuConsole *src, InputEvent *evt)
{
    const char *name;
    int qcode, idx = -1;

    if (src) {
        idx = vmx_console_get_index(src);
    }
    switch (evt->kind) {
    case INPUT_EVENT_KIND_KEY:
        switch (evt->key->key->kind) {
        case KEY_VALUE_KIND_NUMBER:
            qcode = vmx_input_key_number_to_qcode(evt->key->key->number);
            name = QKeyCode_lookup[qcode];
            break;
        case KEY_VALUE_KIND_QCODE:
            name = QKeyCode_lookup[evt->key->key->qcode];
            break;
        case KEY_VALUE_KIND_MAX:
            /* keep gcc happy */
            break;
        }
        break;
    case INPUT_EVENT_KIND_BTN:
        name = InputButton_lookup[evt->btn->button];
        break;
    case INPUT_EVENT_KIND_REL:
        name = InputAxis_lookup[evt->rel->axis];
        break;
    case INPUT_EVENT_KIND_ABS:
        name = InputAxis_lookup[evt->abs->axis];
        break;
    case INPUT_EVENT_KIND_MAX:
        /* keep gcc happy */
        break;
    }
}

static void vmx_input_queue_process(void *opaque)
{
    struct QemuInputEventQueueHead *queue = opaque;
    QemuInputEventQueue *item;

    g_assert(!QTAILQ_EMPTY(queue));
    item = QTAILQ_FIRST(queue);
    //g_assert(item->type == QEMU_INPUT_QUEUE_DELAY);
    QTAILQ_REMOVE(queue, item, node);
    g_free(item);

    while (!QTAILQ_EMPTY(queue)) {
        item = QTAILQ_FIRST(queue);
        switch (item->type) {
        case QEMU_INPUT_QUEUE_DELAY:
            timer_mod(item->timer, vmx_clock_get_ms(QEMU_CLOCK_VIRTUAL)
                      + item->delay_ms);
            return;
        case QEMU_INPUT_QUEUE_EVENT:
            vmx_input_event_send(item->src, item->evt);
            qapi_free_InputEvent(item->evt);
            break;
        case QEMU_INPUT_QUEUE_SYNC:
            vmx_input_event_sync();
            break;
        }
        QTAILQ_REMOVE(queue, item, node);
        g_free(item);
    }
}

static void vmx_input_queue_delay(struct QemuInputEventQueueHead *queue,
                                   QEMUTimer *timer, uint32_t delay_ms)
{
    QemuInputEventQueue *item = g_new0(QemuInputEventQueue, 1);
    bool start_timer = QTAILQ_EMPTY(queue);

    item->type = QEMU_INPUT_QUEUE_DELAY;
    item->delay_ms = delay_ms;
    item->timer = timer;
    QTAILQ_INSERT_TAIL(queue, item, node);

    if (start_timer) {
        timer_mod(item->timer, vmx_clock_get_ms(QEMU_CLOCK_VIRTUAL)
                  + item->delay_ms);
    }
}

static void vmx_input_queue_event(struct QemuInputEventQueueHead *queue,
                                   QemuConsole *src, InputEvent *evt)
{
    QemuInputEventQueue *item = g_new0(QemuInputEventQueue, 1);

    item->type = QEMU_INPUT_QUEUE_EVENT;
    item->src = src;
    item->evt = evt;
    QTAILQ_INSERT_TAIL(queue, item, node);
}

static void vmx_input_queue_sync(struct QemuInputEventQueueHead *queue)
{
    QemuInputEventQueue *item = g_new0(QemuInputEventQueue, 1);

    item->type = QEMU_INPUT_QUEUE_SYNC;
    QTAILQ_INSERT_TAIL(queue, item, node);
}

void vmx_input_event_send(QemuConsole *src, InputEvent *evt)
{
    QemuInputHandlerState *s;

    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }

    vmx_input_event_trace(src, evt);

    /* pre processing */
    if (graphic_rotate && (evt->kind == INPUT_EVENT_KIND_ABS)) {
            vmx_input_transform_abs_rotate(evt);
    }

    /* send event */
    s = vmx_input_find_handler(1 << evt->kind, src);
    if (!s) {
        return;
    }
    s->handler->event(s->dev, src, evt);
    s->events++;
}

void vmx_input_event_sync(void)
{
    QemuInputHandlerState *s;

    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }

    QTAILQ_FOREACH(s, &handlers, node) {
        if (!s->events) {
            continue;
        }
        if (s->handler->sync) {
            s->handler->sync(s->dev);
        }
        s->events = 0;
    }
}

InputEvent *vmx_input_event_new_key(KeyValue *key, bool down)
{
    InputEvent *evt = g_new0(InputEvent, 1);
    evt->key = g_new0(InputKeyEvent, 1);
    evt->kind = INPUT_EVENT_KIND_KEY;
    evt->key->key = key;
    evt->key->down = down;
    return evt;
}

void vmx_input_event_send_key(QemuConsole *src, KeyValue *key, bool down)
{
    InputEvent *evt;
    evt = vmx_input_event_new_key(key, down);
    if (QTAILQ_EMPTY(&kbd_queue)) {
        vmx_input_event_send(src, evt);
        vmx_input_event_sync();
        qapi_free_InputEvent(evt);
    } else {
        vmx_input_queue_event(&kbd_queue, src, evt);
        vmx_input_queue_sync(&kbd_queue);
    }
}

void vmx_input_event_send_key_number(QemuConsole *src, int num, bool down)
{
    KeyValue *key = g_new0(KeyValue, 1);
    key->kind = KEY_VALUE_KIND_NUMBER;
    key->number = num;
    vmx_input_event_send_key(src, key, down);
}

void vmx_input_event_send_key_qcode(QemuConsole *src, QKeyCode q, bool down)
{
    KeyValue *key = g_new0(KeyValue, 1);
    key->kind = KEY_VALUE_KIND_QCODE;
    key->qcode = q;
    vmx_input_event_send_key(src, key, down);
}

void vmx_input_event_send_key_delay(uint32_t delay_ms)
{
    if (!kbd_timer) {
        kbd_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, vmx_input_queue_process,
                                 &kbd_queue);
    }
    vmx_input_queue_delay(&kbd_queue, kbd_timer,
                           delay_ms ? delay_ms : kbd_default_delay_ms);
}

InputEvent *vmx_input_event_new_btn(InputButton btn, bool down)
{
    InputEvent *evt = g_new0(InputEvent, 1);
    evt->btn = g_new0(InputBtnEvent, 1);
    evt->kind = INPUT_EVENT_KIND_BTN;
    evt->btn->button = btn;
    evt->btn->down = down;
    return evt;
}

void vmx_input_queue_btn(QemuConsole *src, InputButton btn, bool down)
{
    InputEvent *evt;
    evt = vmx_input_event_new_btn(btn, down);
    vmx_input_event_send(src, evt);
    qapi_free_InputEvent(evt);
}

void vmx_input_update_buttons(QemuConsole *src, uint32_t *button_map,
                               uint32_t button_old, uint32_t button_new)
{
    InputButton btn;
    uint32_t mask;

    for (btn = 0; btn < INPUT_BUTTON_MAX; btn++) {
        mask = button_map[btn];
        if ((button_old & mask) == (button_new & mask)) {
            continue;
        }
        vmx_input_queue_btn(src, btn, button_new & mask);
    }
}

bool vmx_input_is_absolute(void)
{
    QemuInputHandlerState *s;

    s = vmx_input_find_handler(INPUT_EVENT_MASK_REL | INPUT_EVENT_MASK_ABS,
                                NULL);
    return (s != NULL) && (s->handler->mask & INPUT_EVENT_MASK_ABS);
}

int vmx_input_scale_axis(int value, int size_in, int size_out)
{
    if (size_in < 2) {
        return size_out / 2;
    }
    return (int64_t)value * (size_out - 1) / (size_in - 1);
}

InputEvent *vmx_input_event_new_move(InputEventKind kind,
                                      InputAxis axis, int value)
{
    InputEvent *evt = g_new0(InputEvent, 1);
    InputMoveEvent *move = g_new0(InputMoveEvent, 1);

    evt->kind = kind;
    evt->data = move;
    move->axis = axis;
    move->value = value;
    return evt;
}

void vmx_input_queue_rel(QemuConsole *src, InputAxis axis, int value)
{
    InputEvent *evt;
    evt = vmx_input_event_new_move(INPUT_EVENT_KIND_REL, axis, value);
    vmx_input_event_send(src, evt);
    qapi_free_InputEvent(evt);
}

void vmx_input_queue_abs(QemuConsole *src, InputAxis axis, int value, int size)
{
    InputEvent *evt;
    int scaled = vmx_input_scale_axis(value, size, INPUT_EVENT_ABS_SIZE);
    if (INPUT_AXIS_Z ==  axis)
        scaled = -value;
    evt = vmx_input_event_new_move(INPUT_EVENT_KIND_ABS, axis, scaled);
    vmx_input_event_send(src, evt);
    qapi_free_InputEvent(evt);
}

void vmx_input_check_mode_change(void)
{
    static int current_is_absolute;
    int is_absolute;

    is_absolute = vmx_input_is_absolute();

    if (is_absolute != current_is_absolute) {
        veertu_notifiers_notify(&mouse_mode_notifiers, NULL);
    }

    current_is_absolute = is_absolute;
}

void vmx_add_mouse_mode_change_notifier(Notifier *notify)
{
    veertu_notifiers_add(&mouse_mode_notifiers, notify);
}

void vmx_remove_mouse_mode_change_notifier(Notifier *notify)
{
    veertu_notifiers_remove(notify);
}

MouseInfoList *qmp_query_mice(Error **errp)
{
    MouseInfoList *mice_list = NULL;
    MouseInfoList *info;
    QemuInputHandlerState *s;
    bool current = true;

    QTAILQ_FOREACH(s, &handlers, node) {
        if (!(s->handler->mask &
              (INPUT_EVENT_MASK_REL | INPUT_EVENT_MASK_ABS))) {
            continue;
        }

        info = g_new0(MouseInfoList, 1);
        info->value = g_new0(MouseInfo, 1);
        info->value->index = s->id;
        info->value->name = g_strdup(s->handler->name);
        info->value->absolute = s->handler->mask & INPUT_EVENT_MASK_ABS;
        info->value->current = current;

        current = false;
        info->next = mice_list;
        mice_list = info;
    }

    return mice_list;
}

void do_mouse_set(Monitor *mon, const QDict *qdict)
{
    QemuInputHandlerState *s;
    int index = qdict_get_int(qdict, "index");
    int found = 0;

    QTAILQ_FOREACH(s, &handlers, node) {
        if (s->id != index) {
            continue;
        }
        if (!(s->handler->mask & (INPUT_EVENT_MASK_REL |
                                  INPUT_EVENT_MASK_ABS))) {
            error_report("Input device '%s' is not a mouse", s->handler->name);
            return;
        }
        found = 1;
        vmx_input_handler_activate(s);
        break;
    }

    if (!found) {
        error_report("Mouse at index '%d' not found", index);
    }

    vmx_input_check_mode_change();
}
