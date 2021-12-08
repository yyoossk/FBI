#include <malloc.h>
#include <stdio.h>

#include <3ds.h>

#include "action.h"
#include "../resources.h"
#include "../task/uitask.h"
#include "../../core/core.h"

typedef struct {
    linked_list* items;
    bool unused;

    linked_list contents;

    data_op_data deleteInfo;
} delete_tickets_data;

static void action_delete_tickets_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    delete_tickets_data* deleteData = (delete_tickets_data*) data;

    u32 curr = deleteData->deleteInfo.processed;
    if(curr < deleteData->deleteInfo.total) {
        task_draw_ticket_info(view, ((list_item*) linked_list_get(&deleteData->contents, curr))->data, x1, y1, x2, y2);
    }
}

static Result action_delete_tickets_delete(void* data, u32 index) {
    delete_tickets_data* deleteData = (delete_tickets_data*) data;

    Result res = 0;

    u64 titleId = ((ticket_info*) ((list_item*) linked_list_get(&deleteData->contents, index))->data)->titleId;
    if(R_SUCCEEDED(res = AM_DeleteTicket(titleId))) {
        linked_list_iter iter;
        linked_list_iterate(deleteData->items, &iter);

        while(linked_list_iter_has_next(&iter)) {
            list_item* item = (list_item*) linked_list_iter_next(&iter);
            ticket_info* currInfo = (ticket_info*) item->data;

            if(currInfo->titleId == titleId) {
                linked_list_iter_remove(&iter);
                task_free_ticket(item);
            }
        }
    }

    return res;
}

static Result action_delete_tickets_suspend(void* data, u32 index) {
    return 0;
}

static Result action_delete_tickets_restore(void* data, u32 index) {
    return 0;
}

static bool action_delete_tickets_error(void* data, u32 index, Result res, ui_view** errorView) {
    *errorView = error_display_res(data, action_delete_tickets_draw_top, res, "チケットの削除に失敗しました。");
    return true;
}

static void action_delete_tickets_free_data(delete_tickets_data* data) {
    if(data->unused) {
        task_clear_tickets(&data->contents);
    }

    linked_list_destroy(&data->contents);
    free(data);
}

static void action_delete_tickets_update(ui_view* view, void* data, float* progress, char* text) {
    delete_tickets_data* deleteData = (delete_tickets_data*) data;

    if(deleteData->deleteInfo.finished) {
        ui_pop();
        info_destroy(view);

        if(R_SUCCEEDED(deleteData->deleteInfo.result)) {
            prompt_display_notify("成功", "チケットが削除されました。", COLOR_TEXT, NULL, NULL, NULL);
        }

        action_delete_tickets_free_data(deleteData);

        return;
    }

    if((hidKeysDown() & KEY_B) && !deleteData->deleteInfo.finished) {
        svcSignalEvent(deleteData->deleteInfo.cancelEvent);
    }

    *progress = deleteData->deleteInfo.total > 0 ? (float) deleteData->deleteInfo.processed / (float) deleteData->deleteInfo.total : 0;
    snprintf(text, PROGRESS_TEXT_MAX, "%lu / %lu", deleteData->deleteInfo.processed, deleteData->deleteInfo.total);
}

static void action_delete_tickets_onresponse(ui_view* view, void* data, u32 response) {
    delete_tickets_data* deleteData = (delete_tickets_data*) data;

    if(response == PROMPT_YES) {
        Result res = task_data_op(&deleteData->deleteInfo);
        if(R_SUCCEEDED(res)) {
            info_display("削除", "Bボタンを押してキャンセル。", true, data, action_delete_tickets_update, action_delete_tickets_draw_top);
        } else {
            error_display_res(NULL, NULL, res, "削除操作を開始できませんでした。");

            action_delete_tickets_free_data(deleteData);
        }
    } else {
        action_delete_tickets_free_data(deleteData);
    }
}

typedef struct {
    delete_tickets_data* deleteData;

    const char* message;

    populate_tickets_data popData;
} delete_tickets_loading_data;

static void action_delete_tickets_loading_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    action_delete_tickets_draw_top(view, ((delete_tickets_loading_data*) data)->deleteData, x1, y1, x2, y2);
}

static void action_delete_tickets_loading_update(ui_view* view, void* data, float* progress, char* text)  {
    delete_tickets_loading_data* loadingData = (delete_tickets_loading_data*) data;

    if(loadingData->popData.finished) {
        ui_pop();
        info_destroy(view);

        if(R_SUCCEEDED(loadingData->popData.result)) {
            linked_list_iter iter;
            linked_list_iterate(&loadingData->deleteData->contents, &iter);
            while(linked_list_iter_has_next(&iter)) {
                list_item* item = (list_item*) linked_list_iter_next(&iter);
                ticket_info* info = (ticket_info*) item->data;

                if(info->inUse) {
                    linked_list_iter_remove(&iter);
                    task_free_ticket(item);
                }
            }

            loadingData->deleteData->deleteInfo.total = linked_list_size(&loadingData->deleteData->contents);
            loadingData->deleteData->deleteInfo.processed = loadingData->deleteData->deleteInfo.total;

            prompt_display_yes_no("確認", loadingData->message, COLOR_TEXT, loadingData->deleteData, action_delete_tickets_draw_top, action_delete_tickets_onresponse);
        } else {
            error_display_res(NULL, NULL, loadingData->popData.result, "チケットリストの入力に失敗しました。");

            action_delete_tickets_free_data(loadingData->deleteData);
        }

        free(loadingData);
        return;
    }

    if((hidKeysDown() & KEY_B) && !loadingData->popData.finished) {
        svcSignalEvent(loadingData->popData.cancelEvent);
    }

    snprintf(text, PROGRESS_TEXT_MAX, "チケットリストの取得中...");
}

static void action_delete_tickets_internal(linked_list* items, list_item* selected, const char* message, bool unused) {
    delete_tickets_data* data = (delete_tickets_data*) calloc(1, sizeof(delete_tickets_data));
    if(data == NULL) {
        error_display(NULL, NULL, "削除データの割り当てに失敗しました。");

        return;
    }

    data->items = items;
    data->unused = unused;

    data->deleteInfo.data = data;

    data->deleteInfo.op = DATAOP_DELETE;

    data->deleteInfo.delete = action_delete_tickets_delete;

    data->deleteInfo.suspend = action_delete_tickets_suspend;
    data->deleteInfo.restore = action_delete_tickets_restore;

    data->deleteInfo.error = action_delete_tickets_error;

    data->deleteInfo.finished = false;

    linked_list_init(&data->contents);

    if(unused) {
        delete_tickets_loading_data* loadingData = (delete_tickets_loading_data*) calloc(1, sizeof(delete_tickets_loading_data));
        if(loadingData == NULL) {
            error_display(NULL, NULL, "読み込みデータの割り当てに失敗しました。");

            action_delete_tickets_free_data(data);
            return;
        }

        loadingData->deleteData = data;
        loadingData->message = message;

        loadingData->popData.items = &data->contents;

        Result listRes = task_populate_tickets(&loadingData->popData);
        if(R_FAILED(listRes)) {
            error_display_res(NULL, NULL, listRes, "チケットリストの作成を開始できませんでした。");

            free(loadingData);
            action_delete_tickets_free_data(data);
            return;
        }

        info_display("読み込み中", "Bボタンを押してキャンセル。", false, loadingData, action_delete_tickets_loading_update, action_delete_tickets_loading_draw_top);
    } else {
        linked_list_add(&data->contents, selected);

        data->deleteInfo.total = 1;
        data->deleteInfo.processed = data->deleteInfo.total;

        prompt_display_yes_no("確認", message, COLOR_TEXT, data, action_delete_tickets_draw_top, action_delete_tickets_onresponse);
    }
}

void action_delete_ticket(linked_list* items, list_item* selected) {
    action_delete_tickets_internal(items, selected, "選択したチケットを削除しますか?", false);
}

void action_delete_tickets_unused(linked_list* items, list_item* selected) {
    action_delete_tickets_internal(items, selected, "未使用のチケットをすべて削除しますか?", true);
}
