<script setup>
import { ref, reactive, getCurrentInstance, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import Avatar from '@/components/Avatar.vue';
import { dataType } from 'element-plus/es/components/table-v2/src/common';

const { proxy } = getCurrentInstance();
const route = useRoute();
const router = useRouter();

const loading = ref(false);
const messageListInfo = ref({});
const unReadCount = ref(0);

const api = {
    handleGetNotifications: "/notifications",
    handleMarkAsRead: "/notifications/id/read",
    handleMarkAllAsRead: "/notifications/read-all",
    handleDeleteNotification: "/notifications/id",
    handleDeleteAllReadNotifications: "/notifications/read",
    handleGetUnreadCount: "/notifications/unread-count",
};

const getUnReadNoticeCount = async () => {
    let result = await proxy.Request({
        url: api.handleGetUnreadCount,
        dataType: "json",
        method: 'get',
    });

    if (!result) {
        return 0;
    }
    unReadCount.value = result.data.unread_count;
};

const loadData = async () => {
    console.log("加载通知数据");
    loading.value = true;
    let params = {
        page: messageListInfo.value.page || 1,
        page_size: messageListInfo.value.page_size || 10,
    };
    let result = await proxy.Request({
        dataType: "json",
        method: "GET",
        url: api.handleGetNotifications,
        params: params
    });
    loading.value = false;
    if (!result) {
        return;
    }
    messageListInfo.value = result;
    getUnReadNoticeCount();
};

loadData();

const markAllAsRead = async () => {
    let result = await proxy.Request({
        dataType: "json",
        method: "PUT",
        url: api.handleMarkAllAsRead,
    });

    if (result) {
        proxy.$message.success("全部标记已读成功");
        loadData();
    }
};

const deleteAllNotifications = async () => {
    proxy.$confirm('确认要删除所有已读通知吗？', '提示', {
        type: 'warning',
    }).then(async () => {
        let result = await proxy.Request({
            dataType: "json",
            method: "DELETE",
            url: api.handleDeleteAllReadNotifications,
        });

        if (result) {
            proxy.$message.success("删除成功");
            loadData();
        }
    }).catch(() => { });
};

const readNotification = async (notificationId) => {
    let result = await proxy.Request({
        dataType: "json",
        method: "PUT",
        url: api.handleMarkAsRead,
        params: {
            notification_id: notificationId,
        }
    });
    if (result) {
        const item = messageListInfo.value.data.find(x => x.notification_id === notificationId);
        if (item) {
            item.is_read = 1;
            getUnReadNoticeCount();
        }
    }
};

const deleteNotification = async (notificationId) => {
    let result = await proxy.Request({
        dataType: "json",
        method: "DELETE",
        url: api.handleDeleteNotification,
        params: {
            notification_id: notificationId,
        }
    });

    if (result) {
        proxy.$message.success("删除通知成功");
        loadData();
    }
};

</script>

<template>
    <div class="body-container notice-body" :style="{ width: proxy.globalInfo.bodyWidth + 'px' }">

        <div class="notice-panel">
            <div class="header-actions">
                <div class="text">通知中心</div>
                <div class="btn-group">
                    <el-button type="primary" :disabled="unReadCount == 0" @click="markAllAsRead">已读所有通知</el-button>
                    <el-button type="danger"
                        :disabled="!messageListInfo.total_count || messageListInfo.total_count == 0"
                        @click="deleteAllNotifications">删除所有已读通知</el-button>
                </div>
            </div>
            <DataList :loading="loading" :dataSource="messageListInfo" @loadData="loadData">
                <template #default="{ data }">
                    <div class="message-item">
                        <div class="avatar-box">
                            <Avatar :userId="data.sender_account" :width="50"></Avatar>
                        </div>
                        <div class="message-content">
                            <div :class="['content-text', data.is_read == 0 ? 'unread' : 'read']">
                                <router-link v-if="data.sender_account" :to="`/ucenter/${data.sender_account}`"
                                    class="user-link">
                                    {{ data.sender_username }}
                                </router-link>
                                <span v-else class="system-sender">系统通知</span>
                                <span class="message-body">{{ data.content }}</span>
                            </div>
                            <div class="time-info">{{ data.created_at }}</div>
                        </div>
                        <div class="op-btn">
                            <span v-if="data.is_read == 0" class="btn-text"
                                @click="readNotification(data.notification_id)">已读</span>
                            <span class="btn-text delete" @click="deleteNotification(data.notification_id)">删除</span>
                        </div>
                    </div>
                </template>
            </DataList>
        </div>
    </div>
</template>

<style scoped lang="scss">
.notice-body {
    display: flex;
    margin-top: 10px;
    min-height: 500px;

    .notice-panel {
        flex: 1;
        background: #fff;
        padding: 20px;

        .header-actions {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 20px;
            padding-bottom: 10px;
            border-bottom: 1px solid #f0f0f0;

            .text {
                font-size: 18px;
                font-weight: bold;
            }
        }

        .message-item {
            display: flex;
            padding: 15px 0;
            border-bottom: 1px solid #eee;
            align-items: flex-start;

            .avatar-box {
                margin-right: 15px;
            }

            .message-content {
                flex: 1;
                font-size: 14px;

                .content-text {
                    margin-bottom: 5px;
                    line-height: 1.5;

                    .user-link {
                        color: #333;
                        font-weight: bold;
                        text-decoration: none;
                        margin-right: 5px;

                        &:hover {
                            color: var(--link);
                        }
                    }

                    .system-sender {
                        color: #333;
                        font-weight: bold;
                        margin-right: 5px;
                    }

                    .message-body {
                        margin-left: 5px;
                    }
                }

                .unread {
                    color: #000;
                    font-size: 16px;
                    font-weight: bold;

                    .user-link,
                    .system-sender {
                        font-size: 16px;
                    }
                }

                .read {
                    color: #999;
                    font-size: 14px;

                    .user-link {
                        color: #999;
                    }
                }

                .time-info {
                    font-size: 12px;
                    color: #999;
                }
            }

            .op-btn {
                display: flex;
                align-items: center;
                margin-left: 10px;
                align-self: flex-end;

                .btn-text {
                    cursor: pointer;
                    font-size: 13px;
                    margin-left: 10px;
                    color: var(--link);
                }

                .delete {
                    color: #f56c6c;
                }
            }
        }
    }
}
</style>