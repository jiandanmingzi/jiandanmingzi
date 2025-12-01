<script setup>
import { ref, reactive, getCurrentInstance, onMounted, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useStore } from 'vuex';
import ArticleListItem from '@/views/ArticleListItem.vue';

const { proxy } = getCurrentInstance();
const route = useRoute();
const router = useRouter();
const store = useStore();

const userId = ref(route.params.userId);
const userInfo = ref({});
const activeTab = ref('post');
const articleListInfo = ref({});
const loading = ref(false);

const api = {
    getUserInfo: "/ucenter/getUserInfo",
    loadUserArticles: "/ucenter/loadUserArticles",
};

// 获取用户信息
const loadUserInfo = async () => {
    /*
    let result = await proxy.Request({
        url: api.getUserInfo,
        params: {
            userId: userId.value
        }
    });
    if (!result) {
        return;
    }
    userInfo.value = result.data;
    */

    // 本地模拟数据
    console.log("Loading local user info for:", userId.value);
    userInfo.value = {
        userId: "12345",
        nickName: "程序员老罗",
        sex: 1, // 0:女, 1:男
        personDescription: "会写前端的后端程序员，热爱技术，热爱生活。",
        avatar: "", 
        roleType: "student", // "student":学生, "teacher":老师
        grade: "2020",
        major: "计算机科学与技术",
        postCount: 12,
        likeCount: 123,
        joinTime: "2023-01-01"
    };
};

// 加载文章列表 (发帖/评论/收藏)
const loadArticleList = async () => {
    loading.value = true;
    /*
    let params = {
        pageNo: articleListInfo.value.pageNo || 1,
        type: activeTab.value, // post, comment, collect
        userId: userId.value
    };
    let result = await proxy.Request({
        url: api.loadUserArticles,
        params: params
    });
    loading.value = false;
    if (!result) {
        return;
    }
    articleListInfo.value = result.data;
    */

    // 本地模拟数据
    console.log(`Loading local articles for tab: ${activeTab.value}`);
    setTimeout(() => {
        let mockList = [];
        // 根据不同Tab生成不同的模拟数据标题
        const prefix = activeTab.value === 'post' ? '【原创】' : activeTab.value === 'comment' ? '【评论过】' : '【收藏】';
        
        for (let i = 0; i < 5; i++) {
            mockList.push({
                articleId: `${activeTab.value}_${i}`,
                userId: userInfo.value.userId,
                nickName: userInfo.value.nickName,
                postTime: "2023-02-12 20:21:21",
                boardId: "backend",
                boardName: "Easybbs开发 / 开发资料",
                topType: 0,
                title: `${prefix} Vue3 + SpringBoot 实战开发系列教程 - 第 ${i+1} 篇`,
                summary: "本系列教程将带你从零开始开发一个功能完整的论坛系统，涵盖前后端技术栈...",
                readCount: 2110 + i * 10,
                goodCount: 3 + i,
                commentCount: 1 + i,
            });
        }

        articleListInfo.value = {
            pageNo: 1,
            pageSize: 10,
            totalCount: 20,
            pageTotal: 2,
            dataList: mockList
        };
        loading.value = false;
    }, 500);
};

const changeTab = (tabName) => {
    activeTab.value = tabName;
    // 重置分页并重新加载
    articleListInfo.value = {}; 
    loadArticleList();
};

// 监听路由变化，如果是切换用户，需要重新加载
watch(
    () => route.params.userId,
    (newVal, oldVal) => {
        if (newVal) {
            userId.value = newVal;
            loadUserInfo();
            loadArticleList();
        }
    },
    { immediate: true, deep: true }
);

</script>

<template>
    <div 
        class="body-container ucenter-body"
        :style="{ width: proxy.globalInfo.bodyWidth + 'px' }"
    >
        <!-- 左侧个人信息 -->
        <div class="user-side">
            <div class="avatar-panel">
                <Avatar :width="120" :userId="userInfo.userId"></Avatar>
            </div>
            <div class="nick-name">
                {{ userInfo.nickName }}
                <span v-if="userInfo.sex == 1" class="iconfont icon-man"></span>
                <span v-if="userInfo.sex == 0" class="iconfont icon-woman"></span>
            </div>
            <div class="desc">
                {{ userInfo.personDescription || '这个人很懒，什么都没写' }}
            </div>
            
            <!-- 身份信息区域 -->
            <div class="user-extend-panel">
                <div class="info-item">
                    <span class="label iconfont icon-user"></span>
                    <span class="value">{{ userInfo.roleType == "student" ? '学生' : '老师' }}</span>
                </div>
                <!-- 学生特有信息 -->
                <template v-if="userInfo.roleType == 'student' ">
                    <div class="info-item">
                        <span class="label iconfont icon-school"></span>
                        <span class="value">{{ userInfo.major }}</span>
                    </div>
                    <div class="info-item">
                        <span class="label iconfont icon-date"></span>
                        <span class="value">{{ userInfo.grade }}级</span>
                    </div>
                </template>
            </div>
        </div>

        <!-- 右侧内容列表 -->
        <div class="article-panel">
            <div class="tabs-container">
                <el-tabs v-model="activeTab" @tab-change="changeTab">
                    <el-tab-pane label="发帖" name="post"></el-tab-pane>
                    <el-tab-pane label="评论" name="comment"></el-tab-pane>
                    <el-tab-pane label="收藏" name="collect"></el-tab-pane>
                </el-tabs>
            </div>
            <div class="article-list">
                <DataList 
                    :loading="loading" 
                    :dataSource="articleListInfo" 
                    @loadData="loadArticleList"
                >
                    <template #default="{data}">
                        <ArticleListItem :data="data"></ArticleListItem>
                    </template>
                </DataList>
            </div>
        </div>
    </div>
</template>

<style scoped lang="scss">
.ucenter-body {
    display: flex;
    margin-top: 10px;
    
    .user-side {
        width: 300px;
        margin-right: 10px;
        background: #fff;
        padding: 20px;
        box-sizing: border-box;
        display: flex;
        flex-direction: column;
        align-items: center;

        .avatar-panel {
            margin-bottom: 10px;
        }

        .nick-name {
            font-size: 18px;
            font-weight: bold;
            color: #333;
            display: flex;
            align-items: center;
            margin-bottom: 10px;
            .iconfont {
                margin-left: 5px;
                font-size: 16px;
            }
            .icon-man {
                color: var(--link);
            }
            .icon-woman {
                color: var(--pink);
            }
        }

        .desc {
            font-size: 14px;
            color: #666;
            text-align: center;
            margin-bottom: 20px;
            line-height: 1.5;
        }

        .user-extend-panel {
            width: 100%;
            border-top: 1px solid #f0f0f0;
            padding-top: 15px;
            
            .info-item {
                display: flex;
                align-items: center;
                margin-bottom: 10px;
                font-size: 14px;
                color: #666;
                
                .label {
                    margin-right: 10px;
                    font-size: 16px;
                    color: #888;
                    width: 20px;
                    text-align: center;
                }
                
                .value {
                    flex: 1;
                }
            }
        }
    }

    .article-panel {
        flex: 1;
        background: #fff;
        padding: 0 15px 15px 15px;
        
        .tabs-container {
            :deep(.el-tabs__nav-wrap::after) {
                height: 1px;
                background-color: #f0f0f0;
            }
            :deep(.el-tabs__item) {
                font-size: 16px;
            }
        }
    }
}
</style>