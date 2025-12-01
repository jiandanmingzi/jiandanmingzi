<script setup>
import { ref, reactive, getCurrentInstance, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import Avatar from '@/components/Avatar.vue';

const { proxy } = getCurrentInstance();
const route = useRoute();
const router = useRouter();

const activeTab = ref('reply'); // 默认选中回复我的
const loading = ref(false);
const messageListInfo = ref({});

const tabList = [
    { name: '回复我的', type: 'reply', icon: 'icon-comment' },
    { name: '赞了我的文章', type: 'likePost', icon: 'icon-good' },
    { name: '赞了我的评论', type: 'likeComment', icon: 'icon-good' },
    { name: '系统消息', type: 'system', icon: 'icon-notice' },
];

const api = {
    loadMessage: "/ucenter/loadMessage", // 预留API
};

// 模拟后端数据生成
const mockMessageService = (type, pageNo) => {
    const list = [];
    const count = 10; // 每页条数
    
    for (let i = 1; i <= count; i++) {
        let item = {
            messageId: `${type}_${pageNo}_${i}`,
            createTime: "2023-08-20 14:30:00",
            status: 1, // 1:已读 0:未读
        };

        if (type === 'reply') {
            item.sendUserId = `user_${i}`;
            item.sendUserNickName = `用户${i}`;
            item.articleId = "1001";
            item.articleTitle = "Vue3 + SpringBoot 实战开发系列教程";
            item.messageContent = "博主写得太好了，受益匪浅！请问下一篇什么时候更新？";
        } else if (type === 'likePost') {
            item.sendUserId = `user_${i}`;
            item.sendUserNickName = `点赞狂魔${i}`;
            item.articleId = "1001";
            item.articleTitle = "关于后端接口设计的几点思考";
            item.messageContent = "赞了你的文章";
        } else if (type === 'likeComment') {
            item.sendUserId = `user_${i}`;
            item.sendUserNickName = `路人${i}`;
            item.articleId = "1001";
            item.articleTitle = "Java并发编程实战";
            item.messageContent = "赞了你的评论：确实，这个问题我也遇到过...";
        } else if (type === 'system') {
            item.messageContent = "您的账号已成功通过实名认证，现在可以开始发帖了。";
            item.articleTitle = "系统通知"; // 借用字段显示标题
        }
        list.push(item);
    }

    return {
        dataList: list,
        pageNo: pageNo,
        totalCount: 45, // 假设总数
        pageTotal: 5
    };
};

const loadData = async () => {
    loading.value = true;
    /*
    let params = {
        pageNo: messageListInfo.value.pageNo || 1,
        type: activeTab.value
    };
    let result = await proxy.Request({
        url: api.loadMessage,
        params: params
    });
    loading.value = false;
    if (!result) {
        return;
    }
    messageListInfo.value = result.data;
    */

    // 本地模拟
    console.log(`加载消息列表 -> 类型: ${activeTab.value}, 页码: ${messageListInfo.value.pageNo || 1}`);
    setTimeout(() => {
        messageListInfo.value = mockMessageService(activeTab.value, messageListInfo.value.pageNo || 1);
        loading.value = false;
    }, 500);
};

const changeTab = (type) => {
    activeTab.value = type;
    messageListInfo.value = {}; // 清空旧数据
    loadData();
};

// 监听路由参数变化（如果从Header下拉菜单跳转过来）
watch(
    () => route.params.type,
    (newVal, oldVal) => {
        if (newVal) {
            activeTab.value = newVal;
        }
        loadData();
    },
    { immediate: true, deep: true }
);

</script>

<template>
    <div 
        class="body-container notice-body"
        :style="{ width: proxy.globalInfo.bodyWidth + 'px' }"
    >
        <!-- 左侧导航 -->
        <div class="side-menu">
            <div 
                v-for="item in tabList" 
                :key="item.type"
                :class="['menu-item', activeTab === item.type ? 'active' : '']"
                @click="changeTab(item.type)"
            >
                <span :class="['iconfont', item.icon]"></span>
                {{ item.name }}
            </div>
        </div>

        <!-- 右侧列表 -->
        <div class="notice-panel">
            <div class="panel-title">{{ tabList.find(t => t.type === activeTab)?.name }}</div>
            
            <DataList 
                :loading="loading" 
                :dataSource="messageListInfo" 
                @loadData="loadData"
            >
                <template #default="{data}">
                    <div class="message-item">
                        <!-- 系统消息没有头像 -->
                        <div class="avatar-box" v-if="activeTab !== 'system'">
                            <Avatar :userId="data.sendUserId" :width="50"></Avatar>
                        </div>
                        
                        <div class="message-content">
                            <!-- 回复我的 -->
                            <template v-if="activeTab === 'reply'">
                                <div class="info-line">
                                    <router-link :to="`/ucenter/${data.sendUserId}`" class="user-link">{{ data.sendUserNickName }}</router-link>
                                    <span class="text">回复了你的文章</span>
                                    <router-link :to="`/post/${data.articleId}`" class="article-link">《{{ data.articleTitle }}》</router-link>
                                </div>
                                <div class="reply-content">{{ data.messageContent }}</div>
                            </template>

                            <!-- 赞了文章 -->
                            <template v-if="activeTab === 'likePost'">
                                <div class="info-line">
                                    <router-link :to="`/ucenter/${data.sendUserId}`" class="user-link">{{ data.sendUserNickName }}</router-link>
                                    <span class="text">赞了你的文章</span>
                                    <router-link :to="`/post/${data.articleId}`" class="article-link">《{{ data.articleTitle }}》</router-link>
                                </div>
                            </template>

                            <!-- 赞了评论 -->
                            <template v-if="activeTab === 'likeComment'">
                                <div class="info-line">
                                    <router-link :to="`/ucenter/${data.sendUserId}`" class="user-link">{{ data.sendUserNickName }}</router-link>
                                    <span class="text">赞了你在文章</span>
                                    <router-link :to="`/post/${data.articleId}`" class="article-link">《{{ data.articleTitle }}》</router-link>
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