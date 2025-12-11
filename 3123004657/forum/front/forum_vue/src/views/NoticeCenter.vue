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

const api = {
    handleGetNotifications: "/notifications",
    handleMarkAsRead: "/notifications/id/read",
    handleMarkAllAsRead: "/notifications/read-all",
    handleDeleteNotification: "/notifications/id",
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
    messageListInfo.value = result.data;

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

        <!-- 右侧列表 -->
        <div class="notice-panel">
            <DataList :loading="loading" :dataSource="messageListInfo" @loadData="loadData">
                <template #default="{ data }">
                    <div class="message-item">
                        <!-- 系统消息没有头像 -->
                        <div class="avatar-box" v-if="activeTab !== 'system'">
                            <Avatar :userId="data.sendUserId" :width="50"></Avatar>
                        </div>

                        <div class="message-content">
                            <!-- 回复我的 -->
                            <template v-if="activeTab === 'reply'">
                                <div class="info-line">
                                    <router-link :to="`/ucenter/${data.sendUserId}`" class="user-link">{{
                                        data.sendUserNickName }}</router-link>
                                    <span class="text">回复了你的文章</span>
                                    <router-link :to="`/post/${data.articleId}`" class="article-link">《{{
                                        data.articleTitle }}》</router-link>
                                </div>
                                <div class="reply-content">{{ data.messageContent }}</div>
                            </template>

                            <!-- 赞了文章 -->
                            <template v-if="activeTab === 'likePost'">
                                <div class="info-line">
                                    <router-link :to="`/ucenter/${data.sendUserId}`" class="user-link">{{
                                        data.sendUserNickName }}</router-link>
                                    <span class="text">赞了你的文章</span>
                                    <router-link :to="`/post/${data.articleId}`" class="article-link">《{{
                                        data.articleTitle }}》</router-link>
                                </div>
                            </template>

                            <!-- 赞了评论 -->
                            <template v-if="activeTab === 'likeComment'">
                                <div class="info-line">
                                    <router-link :to="`/ucenter/${data.sendUserId}`" class="user-link">{{
                                        data.sendUserNickName }}</router-link>
                                    <span class="text">赞了你在文章</span>
                                    <router-link :to="`/post/${data.articleId}`" class="article-link">《{{
                                        data.articleTitle }}》</router-link>
                                    <span class="text">下的评论</span>
                                </div>
                                <div class="reply-content">{{ data.messageContent }}</div>
                            </template>

                            <!-- 系统消息 -->
                            <template v-if="activeTab === 'system'">
                                <div class="info-line">
                                    <span class="system-title">{{ data.articleTitle }}</span>
                                </div>
                                <div class="reply-content">{{ data.messageContent }}</div>
                            </template>

                            <div class="time-info">{{ data.createTime }}</div>
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

    .side-menu {
        width: 200px;
        background: #fff;
        margin-right: 10px;
        padding: 10px 0;

        .menu-item {
            padding: 15px 20px;
            cursor: pointer;
            font-size: 14px;
            color: #333;
            display: flex;
            align-items: center;

            .iconfont {
                margin-right: 10px;
                font-size: 18px;
            }

            &:hover {
                background: #f5f7fa;
                color: var(--link);
            }

            &.active {
                background: #e6f7ff; // 浅蓝色背景
                color: var(--link);
                border-right: 3px solid var(--link);
            }
        }
    }

    .notice-panel {
        flex: 1;
        background: #fff;
        padding: 20px;

        .panel-title {
            font-size: 18px;
            font-weight: bold;
            margin-bottom: 20px;
            padding-bottom: 10px;
            border-bottom: 1px solid #f0f0f0;
        }

        .message-item {
            display: flex;
            padding: 15px 0;
            border-bottom: 1px solid #eee;

            .avatar-box {
                margin-right: 15px;
            }

            .message-content {
                flex: 1;
                font-size: 14px;

                .info-line {
                    margin-bottom: 8px;

                    .user-link {
                        color: #333;
                        font-weight: bold;
                        text-decoration: none;
                        margin-right: 5px;

                        &:hover {
                            color: var(--link);
                        }
                    }

                    .text {
                        color: #666;
                        margin: 0 5px;
                    }

                    .article-link {
                        color: var(--link);
                        text-decoration: none;

                        &:hover {
                            text-decoration: underline;
                        }
                    }

                    .system-title {
                        font-weight: bold;
                        color: #333;
                    }
                }

                .reply-content {
                    background: #f9f9f9;
                    padding: 10px;
                    border-radius: 4px;
                    color: #666;
                    margin-bottom: 8px;
                    line-height: 1.5;
                }

                .time-info {
                    font-size: 12px;
                    color: #999;
                }
            }
        }
    }
}
</style>