<script setup>
import { ref, reactive, getCurrentInstance, watch } from 'vue';
import ArticleListItem from '@/views/ArticleListItem.vue';
import Avatar from '@/components/Avatar.vue';

const { proxy } = getCurrentInstance();

const keyword = ref("");
const searchType = ref(0); // 0: 帖子, 1: 用户
const loading = ref(false);
const resultListInfo = ref({
    page: 1,
    page_size: 10,
    total_count: 0,
    data: []
});

const allDataList = ref([]);

const api = {
    handleSearchPosts: "/posts/search",
    handleSearchUsers: "/users/search",
};

// 切换搜索类型 (帖子/用户)
const changeSearchType = (type) => {
    searchType.value = type;
    // 重置分页状态
    resultListInfo.value = {
        page: 1,
        page_size: 10,
        total_count: 0,
        data: []
    };
    allDataList.value = [];
    search();
};

// 前端分页处理逻辑
const handlePageChange = () => {
    const { page, page_size } = resultListInfo.value;
    const start = (page - 1) * page_size;
    const end = start + page_size;
    resultListInfo.value.data = allDataList.value.slice(start, end);
};

// 执行搜索
const search = async () => {
    loading.value = true;
    let result;
    if (resultListInfo.value.data.length === 0) {
        resultListInfo.value.page = 1;
    }
    if (searchType.value === 0) {
        // 搜索帖子
        result = await proxy.Request({
            url: api.handleSearchPosts,
            method: "POST",
            params: {
                keyword: keyword.value,
                page_size: 100,
            },
        });
    } else {
        // 搜索用户
        result = await proxy.Request({
            url: api.handleSearchUsers,
            method: "POST",
            params: {
                keyword: keyword.value,
                page_size: 100,
            },
        });
    }
    if (!result) {
        loading.value = false;
        return;
    }
    allDataList.value = result.data;
    resultListInfo.value.total_count = result.data.length;

    // 执行前端分页
    handlePageChange();

    loading.value = false;
};
</script>

<template>
    <div class="body-container search-body" :style="{ width: proxy.globalInfo.bodyWidth + 'px' }">
        <!-- 顶部搜索框区域 -->
        <div class="search-panel">
            <el-input v-model="keyword" size="large" placeholder="请输入关键词搜索" @keyup.enter="search" clearable>
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

        <!-- 搜索结果列表 -->
        <div class="result-list">
            <DataList :loading="loading" :dataSource="resultListInfo" @loadData="handlePageChange">
                <template #default="{ data }">
                    <!-- 帖子列表项 -->
                    <ArticleListItem v-if="searchType === 0" :data="data"></ArticleListItem>

                    <!-- 用户列表项 -->
                    <div v-if="searchType === 1" class="user-item">
                        <div class="avatar-box">
                            <Avatar :userId="data.account" :width="50"></Avatar>
                        </div>
                        <div class="user-info">
                            <div class="nick-name">
                                <router-link :to="`/ucenter/${data.account}`" class="link">{{ data.username
                                    }}</router-link>
                            </div>
                            <div class="desc">{{ data.bio || '这个人很懒，什么都没写' }}</div>
                            <div class="count-info">
                                <span>{{ data.role === "student" ? "学生" : "老师" }}</span>
                                <div v-if="data.role === 'student'">
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