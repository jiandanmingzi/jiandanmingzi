<script setup>
import { ref, reactive, getCurrentInstance, onMounted, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { useStore } from 'vuex';
import ArticleListItem from '@/views/ArticleListItem.vue';
import { dataType } from 'element-plus/es/components/table-v2/src/common';

const { proxy } = getCurrentInstance();
const route = useRoute();
const router = useRouter();
const store = useStore();

const postCount = ref(0);
const loadType = ref("ucenter");
const userId = ref(route.params.userId);
const userInfo = ref({});
const activeTab = ref('post');
const articleListInfo = ref({});
const loading = ref(false);

const dialogConfig = reactive({
    show: false,
    title: "修改个人信息",
    buttons: [
        {
            type: "primary",
            text: "确定",
            click: () => {
                updateUserInfo();
            },
        },
    ],
});
const formData = ref({});

const api = {
    handleGetUserInfo: "/users/id",
    handleUpdateUserInfo: "/users/id",
    handleGetUserPosts: "/users/id/posts",
    handleGetUserPostCount: "/users/id/posts/count",
    handleGetUserComments: "/users/id/comments",
    handleGetUserFavorites: "/users/id/favorites",
    handleGetUserLikes: "/users/id/likes",
    handleGetUserCommentLikes: "/users/id/comment-likes",
    handleGetUserBasicInfo: "/users/basic-info",
    handleGetCommentDetail: "/comments/detail",
    handleDeleteComment: "/comments/id"
};

const getPostCount = async () => {
    let result = await proxy.Request({
        url: api.handleGetUserPostCount,
        method: "GET",
    });
    if (!result) {
        return;
    }
    postCount.value = result.data.post_count;
};

const updateUserInfoHandler = () => {
    formData.value = {
        username: userInfo.value.username,
        bio: userInfo.value.bio
    };
    dialogConfig.show = true;
};

const updateUserInfo = async () => {
    let result = await proxy.Request({
        url: api.handleUpdateUserInfo,
        method: "PUT",
        dataType: 'json',
        params: {
            username: formData.value.username,
            bio: formData.value.bio
        }
    });
    if (!result) {
        return;
    }
    proxy.Message.success("修改成功");
    dialogConfig.show = false;
    loadUserInfo();
};

// 获取用户信息
const loadUserInfo = async () => {
    let result;
    if (loadType.value == "personalCenter") {
        result = await proxy.Request({
            url: api.handleGetUserInfo,
            method: "GET",
        });
    } else {
        result = await proxy.Request({
            url: api.handleGetUserBasicInfo,
            method: "POST",
            dataType: "json",
            params: {
                account: userId.value
            }
        });
    }
    if (!result) {
        return;
    }
    userInfo.value = result.data;
};

// 加载文章列表 (发帖/评论/收藏)
const loadArticleList = async () => {
    const tempApi = ref();
    if (activeTab.value == "post") {
        tempApi.value = api.handleGetUserPosts;
    } else if (activeTab.value == "comment") {
        tempApi.value = api.handleGetUserComments;
    } else if (activeTab.value == "favourite") {
        tempApi.value = api.handleGetUserFavorites;
    } else if (activeTab.value == "likes") {
        tempApi.value = api.handleGetUserLikes;
    }
    loading.value = true;
    let result = await proxy.Request({
        url: tempApi.value,
        method: "GET",
        params: {
            page: articleListInfo.value.page || 1,
        }
    });
    loading.value = false;
    if (!result) {
        return;
    }
    articleListInfo.value = result;
    if (activeTab.value == "comment" && articleListInfo.value.data) {
        for (let item of articleListInfo.value.data) {
            if (item.parent_id && item.parent_id > 0) {
                let commentDetailResult = await proxy.Request({
                    url: api.handleGetCommentDetail,
                    method: "POST",
                    params: {
                        comment_id: item.parent_id
                    }
                });
                if (commentDetailResult && commentDetailResult.status == 'success') {
                    item.parent_comment = commentDetailResult.data;
                }
            }
        }
    }
};

const handleDeleteComment = async (commentId) => {
    proxy.$confirm('确认要删除这条评论吗？', '提示', {
        type: 'warning',
    }).then(async () => {
        let result = await proxy.Request({
            url: api.handleDeleteComment,
            method: "DELETE",
            params: {
                comment_id: commentId
            }
        });
        if (result) {
            proxy.$message.success("删除评论成功");
            loadArticleList();
        }
    }).catch(() => { });
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
            loadType.value = "ucenter";
            if (store.getters.getLoginUserInfo && store.getters.getLoginUserInfo.account == newVal) {
                loadType.value = "personalCenter";
                loadArticleList();
                getPostCount();
            }
        } else {
            userId.value = null;
            loadType.value = "personalCenter";
            loadArticleList();
            getPostCount();
        }
        loadUserInfo();
    },
    { immediate: true, deep: true }
);

watch(
    () => store.state.loginUserInfo,
    (newVal) => {
        // 如果用户信息加载完成，且当前查看的是自己的主页
        if (newVal && newVal.data.account == userId.value) {
            loadType.value = "personalCenter";
            // 重新加载以获取完整权限的数据
            loadUserInfo();
            loadArticleList();
            getPostCount();
        }
    },
    { immediate: true, deep: true }
);
</script>

<template>
    <div class="body-container ucenter-body" :style="{ width: proxy.globalInfo.bodyWidth + 'px' }">
        <!-- 左侧个人信息 -->
        <div class="user-side">
            <div class="avatar-panel">
                <div class="avatar-inner">
                    <Avatar :width="120" :userId="userInfo.account"></Avatar>
                    <div class="edit-btn" v-if="loadType == 'personalCenter'" @click="updateUserInfoHandler">
                        修改
                    </div>
                </div>
            </div>
            <div class="nick-name">
                {{ userInfo.username }}
                <br />
                {{ userInfo.account }}
            </div>
            <div class="desc">
                {{ userInfo.bio || '这个人很懒，什么都没写' }}
            </div>
            <div class="post-count" v-if="loadType == 'personalCenter'">
                贴子数: {{ postCount }}
            </div>

            <!-- 身份信息区域 -->
            <div class="user-extend-panel">
                <div class="info-item">
                    <span class="label iconfont icon-user"></span>
                    <span class="value">{{ userInfo.role == "student" ? '学生' : '老师' }}</span>
                </div>
                <!-- 学生特有信息 -->
                <template v-if="userInfo.role == 'student'">
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
        <div class="article-panel" v-if="loadType == 'personalCenter'">
            <div class="tabs-container">
                <el-tabs v-model="activeTab" @tab-change="changeTab">
                    <el-tab-pane label="发帖" name="post"></el-tab-pane>
                    <el-tab-pane label="点赞" name="likes"></el-tab-pane>
                    <el-tab-pane label="评论" name="comment"></el-tab-pane>
                    <el-tab-pane label="收藏" name="favourite"></el-tab-pane>
                </el-tabs>
            </div>
            <div class="article-list" v-if="activeTab == 'post' || activeTab == 'favourite' || activeTab == 'likes'">
                <DataList :loading="loading" :dataSource="articleListInfo" @loadData="loadArticleList">
                    <template #default="{ data }">
                        <ArticleListItem :data="data"></ArticleListItem>
                    </template>
                </DataList>
            </div>
            <div class="article-list" v-else-if="activeTab == 'comment'">
                <DataList :loading="loading" :dataSource="articleListInfo" @loadData="loadArticleList">
                    <template #default="{ data }">
                        <div class="comment-item">
                            <div class="comment-content">
                                <div class="content-text">
                                    {{ data.content }}
                                </div>
                                <div class="parent-comment"
                                    v-if="data.parent_id && data.parent_id != 0 && data.parent_comment">
                                    回复 <span class="parent-user">@{{ data.parent_comment.username }}</span>: {{
                                        data.parent_comment.content }}
                                </div>
                                <div class="comment-info">
                                    <span class="time">{{ data.created_at }}</span>
                                    <router-link :to="`/post/${data.post_id}`" class="post-link">查看原帖</router-link>
                                </div>
                            </div>
                            <div class="op-btn">
                                <span class="iconfont icon-del" @click="handleDeleteComment(data.comment_id)">删除</span>
                            </div>
                        </div>
                    </template>
                </DataList>
            </div>
        </div>
        <div class="article-panel" v-else>
            仅用户本人可查看
        </div>
    </div>

    <Dialog :show="dialogConfig.show" :title="dialogConfig.title" :buttons="dialogConfig.buttons" width="400px"
        :showCancel="true" @close="dialogConfig.show = false">
        <el-form :model="formData" label-width="60px">
            <el-form-item label="昵称">
                <el-input v-model="formData.username" placeholder="请输入昵称"></el-input>
            </el-form-item>
            <el-form-item label="简介">
                <el-input v-model="formData.bio" type="textarea" :rows="5" placeholder="请输入简介" maxlength="100"
                    show-word-limit></el-input>
            </el-form-item>
        </el-form>
    </Dialog>
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

            .avatar-inner {
                position: relative;
                display: inline-block;

                .edit-btn {
                    position: absolute;
                    top: 0;
                    right: -40px;
                    font-size: 14px;
                    color: var(--link);
                    cursor: pointer;
                }
            }
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

        .comment-item {
            padding: 15px 0;
            border-bottom: 1px solid #eee;
            display: flex;
            justify-content: space-between;

            .comment-content {
                flex: 1;

                .content-text {
                    font-size: 15px;
                    color: #333;
                    margin-bottom: 8px;
                }

                .parent-comment {
                    background: #f5f7fa;
                    padding: 10px;
                    border-radius: 4px;
                    font-size: 13px;
                    color: #666;
                    margin-bottom: 8px;

                    .parent-user {
                        color: var(--link);
                        margin-right: 5px;
                    }
                }

                .comment-info {
                    font-size: 13px;
                    color: #999;

                    .time {
                        margin-right: 15px;
                    }

                    .post-link {
                        color: var(--link);
                        text-decoration: none;

                        &:hover {
                            text-decoration: underline;
                        }
                    }
                }
            }

            .op-btn {
                display: flex;
                align-items: flex-end;
                margin-left: 15px;

                .icon-del {
                    color: #f56c6c;
                    cursor: pointer;
                    font-size: 13px;

                    &::before {
                        margin-right: 3px;
                    }
                }
            }
        }
    }
}
</style>