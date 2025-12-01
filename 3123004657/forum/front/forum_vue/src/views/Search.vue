<script setup>
import { ref, reactive, getCurrentInstance, watch } from 'vue';
import ArticleListItem from '@/views/ArticleListItem.vue';
import Avatar from '@/components/Avatar.vue';

const { proxy } = getCurrentInstance();

const keyword = ref("");
const searchType = ref(0); // 0: 帖子, 1: 用户
const orderType = ref(0); // 0:最热 1:发布时间 2:最新发布 3:最近回复
const loading = ref(false);
const resultListInfo = ref({});

const api = {
    search: "/api/search", // 预留API
};

// 切换搜索类型 (帖子/用户)
const changeSearchType = (type) => {
    searchType.value = type;
    // 切换类型后重置排序和列表
    orderType.value = 0;
    resultListInfo.value = {};
    search();
};

// 切换排序方式 (仅帖子有效)
const changeOrderType = (type) => {
    orderType.value = type;
    search();
};

// 执行搜索
const search = async () => {
    loading.value = true;
    
    /* 
    // API 调用逻辑预留
    let params = {
        keyword: keyword.value,
        type: searchType.value,
        pageNo: resultListInfo.value.pageNo || 1,
    };
    if (searchType.value == 0) {
        params.orderType = orderType.value;
    }
    
    let result = await proxy.Request({
        url: api.search,
        params: params
    });
    
    if (!result) {
        loading.value = false;
        return;
    }
    resultListInfo.value = result.data;
    */

    // --- 本地模拟数据开始 ---
    console.log(`正在搜索 -> 关键词: ${keyword.value}, 类型: ${searchType.value === 0 ? '帖子' : '用户'}, 排序: ${orderType.value}`);
    
    setTimeout(() => {
        if (searchType.value === 0) {
            // 模拟帖子数据
            let mockPosts = [];
            for (let i = 1; i <= 10; i++) {
                mockPosts.push({
                    articleId: `post_${i}`,
                    userId: `user_${i}`,
                    nickName: `测试用户${i}`,
                    postTime: "2023-05-20 12:00",
                    boardId: "campus",
                    boardName: "校园生活",
                    topType: 0,
                    title: `关于搜索功能的测试帖子 - ${keyword.value || '无关键词'} - ${i}`,
                    summary: "这是一个模拟的搜索结果摘要，用于展示搜索界面的布局效果。包含关键词高亮逻辑（需后端支持）...",
                    readCount: 100 + i,
                    goodCount: 10 + i,
                    commentCount: 5 + i,
                });
            }
            resultListInfo.value = {
                pageNo: 1,
                totalCount: 50,
                pageTotal: 5,
                dataList: mockPosts
            };
        } else {
            // 模拟用户数据
            let mockUsers = [];
            for (let i = 1; i <= 10; i++) {
                mockUsers.push({
                    userId: `user_${i}`,
                    nickName: `搜索到的用户${i}`,
                    personDescription: "这是一个热爱编程的同学，正在学习Vue3和SpringBoot。",
                    avatar: "", 
                    roleType: "student",
                    major: "计算机科学与技术",
                    grade: "2020"
                });
            }
            resultListInfo.value = {
                pageNo: 1,
                totalCount: 20,
                pageTotal: 2,
                dataList: mockUsers
            };
        }
        loading.value = false;
    }, 500);
    // --- 本地模拟数据结束 ---
};

// 初始加载
search();

</script>

<template>
    <div 
        class="body-container search-body"
        :style="{ width: proxy.globalInfo.bodyWidth + 'px' }"
    >
        <!-- 顶部搜索框区域 -->
        <div class="search-panel">
            <el-input 
                v-model="keyword" 
                size="large" 
                placeholder="请输入关键词搜索" 
                @keyup.enter="search"
                clearable
            >
                <template #append>
                    <el-button type="primary" @click="search">
                        <span class="iconfont icon-search"></span> 搜索
                    </el-button>
                </template>
            </el-input>
        </div>

        <!-- 搜索类型切换 Tab -->
        <div class="type-tab-panel">
            <div :class="['tab-item', searchType === 0 ? 'active' : '']" @click="changeSearchType(0)">帖子</div>
            <div :class="['tab-item', searchType === 1 ? 'active' : '']" @click="changeSearchType(1)">用户</div>
        </div>

        <!-- 帖子排序筛选 (仅在搜索帖子时显示) -->
        <div class="order-tab-panel" v-if="searchType === 0">
            <div :class="['tab', orderType === 0 ? 'active' : '']" @click="changeOrderType(0)">最热</div>
            <el-divider direction="vertical"></el-divider>
            <div :class="['tab', orderType === 1 ? 'active' : '']" @click="changeOrderType(1)">发布时间</div>
            <el-divider direction="vertical"></el-divider>
            <div :class="['tab', orderType === 2 ? 'active' : '']" @click="changeOrderType(2)">最新发布</div>
            <el-divider direction="vertical"></el-divider>
            <div :class="['tab', orderType === 3 ? 'active' : '']" @click="changeOrderType(3)">最近回复</div>
        </div>

        <!-- 搜索结果列表 -->
        <div class="result-list">
            <DataList 
                :loading="loading" 
                :dataSource="resultListInfo" 
                @loadData="search"
            >
                <template #default="{data}">
                    <!-- 帖子列表项 -->
                    <ArticleListItem v-if="searchType === 0" :data="data"></ArticleListItem>
                    
                    <!-- 用户列表项 -->
                    <div v-if="searchType === 1" class="user-item">
                        <div class="avatar-box">
                            <Avatar :userId="data.userId" :width="50"></Avatar>
                        </div>
                        <div class="user-info">
                            <div class="nick-name">
                                <router-link :to="`/ucenter/${data.userId}`" class="link">{{ data.nickName }}</router-link>
                            </div>
                            <div class="desc">{{ data.personDescription || '这个人很懒，什么都没写' }}</div>
                            <div class="count-info">
                                <span>{{ data.roleType === "student" ? "学生" : "老师" }}</span>
                                <div v-if="data.roleType == 'student'">
                                    <el-divider direction="vertical"></el-divider>
                                    <span>专业: {{ data.major }}</span>
                                    <el-divider direction="vertical"></el-divider>
                                    <span>年级: {{ data.grade }}级</span>
                                </div>
                            </div>
                        </div>
                    </div>
                </template>
            </DataList>
        </div>
    </div>
</template>

<style scoped lang="scss">
.search-body {
    background: #fff;
    padding: 20px;
    min-height: calc(100vh - 200px);
    margin-top: 10px;

    .search-panel {
        width: 60%;
        margin: 0 auto 20px auto;
        .icon-search {
            margin-right: 5px;
        }
    }

    .type-tab-panel {
        display: flex;
        border-bottom: 2px solid #f0f0f0;
        margin-bottom: 15px;
        .tab-item {
            font-size: 16px;
            padding: 10px 20px;
            cursor: pointer;
            position: relative;
            color: #333;
            &.active {
                color: var(--link);
                font-weight: bold;
                &::after {
                    content: "";
                    position: absolute;
                    bottom: -2px;
                    left: 0;
                    width: 100%;
                    height: 2px;
                    background: var(--link);
                }
            }
        }
    }

    .order-tab-panel {
        display: flex;
        align-items: center;
        padding: 10px 0;
        font-size: 14px;
        border-bottom: 1px solid #eee;
        margin-bottom: 10px;
        .tab {
            cursor: pointer;
            padding: 0 10px;
            color: #666;
            &.active {
                color: var(--link);
            }
        }
    }

    .result-list {
        .user-item {
            display: flex;
            padding: 15px;
            border-bottom: 1px solid #eee;
            align-items: center;
            &:hover {
                background: #fafafa;
            }
            .avatar-box {
                margin-right: 15px;
            }
            .user-info {
                flex: 1;
                .nick-name {
                    font-size: 16px;
                    font-weight: bold;
                    margin-bottom: 5px;
                    .link {
                        color: #333;
                        text-decoration: none;
                        &:hover {
                            color: var(--link);
                        }
                    }
                }
                .desc {
                    font-size: 13px;
                    color: #888;
                    margin-bottom: 5px;
                    overflow: hidden;
                    text-overflow: ellipsis;
                    white-space: nowrap;
                    max-width: 600px;
                }
                .count-info {
                    font-size: 12px;
                    color: #999;
                    display: flex;
                    align-items: center;
                }
            }
        }
    }
}
</style>