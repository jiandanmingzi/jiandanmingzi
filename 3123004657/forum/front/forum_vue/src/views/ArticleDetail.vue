<script setup>
import { ref, reactive, getCurrentInstance } from 'vue';
import { useRouter, useRoute } from 'vue-router';
import { watch } from 'vue';
import { useStore } from 'vuex';
import CommentList from './CommentList.vue';
const { proxy } = getCurrentInstance();
const route = useRoute();
const store = useStore();

const articleInfo = ref({});
const liked = ref(false);
const collected = ref(false);

const api = {
    handleGetPostDetail: '/posts/handleGetPostDetail',
    handleToggleLike: '/posts/id/like',
    handleToggleFavorite: '/posts/id/collect',
    handleIsPostLiked: '/posts/id/is-liked',
    handleIsPostFavorited: '/posts/id/is-favorited',
}

const getArticleDetail = async (articleId) => {
    let result = await proxy.Request({
        url: api.handleGetPostDetail,
        dataType: "json",
        params: {
            post_id: articleId,
        },
    });

    if (!result) {
        return;
    }
    articleInfo.value = result.data;
    store.commit("saveBoardId", result.data.category);
};

const getIsLikedAndCollected = async (articleId) => {
    let likeResult = await proxy.Request({
        url: api.handleIsPostLiked,
        dataType: "json",
        params: {
            post_id: articleId,
        },
    });

    if (likeResult && likeResult.is_liked) {
        liked.value = true;
    } else {
        liked.value = false;
    }

    let collectResult = await proxy.Request({
        url: api.handleIsPostFavorited,
        dataType: "json",
        params: {
            post_id: articleId,
        },
    });

    if (collectResult && collectResult.is_favorited) {
        collected.value = true;
    } else {
        collected.value = false;
    }
};

watch(
    () => route.params,
    (newVal, oldVal) => {
        if (newVal.articleId) {
            getArticleDetail(newVal.articleId);
            getIsLikedAndCollected(newVal.articleId);
        }
    },
    {
        immediate: true,
    }
);
const quickPanelLeft = (window.innerWidth - proxy.globalInfo.bodyWidth) / 2 - 110;

const goToPosition = (id) => {
    document.querySelector('#' + id).scrollIntoView({ behavior: 'smooth' });
};

const doLikeHandler = async () => {
    if (!store.getters.getLoginUserInfo) {
        proxy.$message.warning("请先登录！");
        store.commit("showLoginDialog", true);
        return;
    }
    let result = await proxy.Request({
        url: api.handleToggleLike,
        dataType: "json",
        params: {
            post_id: articleInfo.value.post_id,
        },
        method: 'post',
    });

    if (!result) {
        return;
    }
    if (result.data.status == 'success') {
        liked.value = !liked.value;
        if (liked.value) {
            articleInfo.value.like_count += 1;
        } else {
            articleInfo.value.like_count -= 1;
        }
    }
};

const doCollectHandler = async () => {
    if (!store.getters.getLoginUserInfo) {
        proxy.$message.warning("请先登录！");
        store.commit("showLoginDialog", true);
        return;
    }
    let result = await proxy.Request({
        url: api.handleToggleFavorite,
        dataType: "json",
        params: {
            post_id: articleInfo.value.post_id,
        },
        method: 'post',
    });

    if (!result) {
        return;
    }
    if (result.data.status == 'success') {
        collected.value = !collected.value;
    }
};
</script>

<template>
    <div class="body-container article-list-body" :style="{ width: proxy.globalInfo.bodyWidth + 'px' }">
        <div class="detail-container" :style="{ width: proxy.globalInfo.bodyWidth - 300 + 'px' }">
            <div class="article-detail">
                <div class="title">{{ articleInfo.title }}</div>
                <div class="user-info">
                    <Avatar :userId="articleInfo.account"></Avatar>
                    <div class="article-user-info">
                        <router-link class="nick-name" :to="`/ucenter/${articleInfo.account}`">
                            {{ articleInfo.username }}
                        </router-link>
                        <div class="article-info">
                            <span>{{ articleInfo.created_at }}</span>
                            <span class="iconfont icon-eye-solid">
                                {{ articleInfo.view_count == 0 ? "阅读" : articleInfo.view_count }}
                            </span>
                        </div>
                    </div>
                </div>
                <div class="detail" id="detail">
                    <v-md-editor :model-value="articleInfo.content" mode="preview"></v-md-editor>
                </div>
            </div>
            <div class="comment-panel" id="view-comment">
                <CommentList v-if="articleInfo.post_id" :articleId="articleInfo.post_id"
                    :articleUserId="articleInfo.account" :totalCount="articleInfo.comment_count">
                </CommentList>
            </div>
        </div>
    </div>
    <div class="quick-panel" :style="{ left: quickPanelLeft + 'px' }">
        <el-badge :value="articleInfo.like_count" type="info" :hidden="!articleInfo.like_count > 0">
            <div class="quick-item" @click="doLikeHandler">
                <span :class="['iconfont icon-good', liked ? 'liked' : '']"></span>
            </div>
        </el-badge>
        <el-badge :value="articleInfo.comment_count" type="info" :hidden="!articleInfo.comment_count > 0">
            <div class="quick-item" @click="goToPosition('view-comment')">
                <span class="iconfont icon-comment"></span>
            </div>
        </el-badge>
        <div class="quick-item" @click="doCollectHandler">
            <span :class="['iconfont icon-eye-solid', collected ? 'collected' : '']"></span>
        </div>
    </div>
</template>

<style scoped lang="scss">
.body-container {
    .detail-container {
        .article-detail {
            padding: 15px;
            background: #fff;

            .title {
                font-weight: bolder;
            }

            .user-info {
                margin-top: 15px;
                display: flex;
                padding-bottom: 10px;
                border-bottom: 1px solid #ddd;

                .article-user-info {
                    margin-left: 10px;

                    .nick-name {
                        text-decoration: none;
                        color: #4e5969;
                        font-size: 15px;
                    }

                    .nick-name:hover {
                        color: var(--link);
                    }

                    .article-info {
                        margin-top: 5px;
                        font-size: 13px;
                        color: var(--text2);
                    }

                    .iconfont {
                        margin-left: 10px;
                    }

                    .iconfont::before {
                        padding-right: 3px;
                    }
                }
            }

            .detail {
                letter-spacing: 1px;
                line-height: 15px;
            }
        }

        .comment-panel {
            margin-top: 20px;
            background: #fff;
        }

    }
}

.quick-panel {
    position: absolute;
    width: 50px;
    top: 150px;
    text-align: center;

    .el-badge__content.is-fixed {
        top: 5px;
        right: 15px;
    }

    .quick-item {
        width: 50px;
        height: 50px;
        display: flex;
        justify-content: center;
        align-items: center;
        border-radius: 50%;
        background: #fff;
        margin-bottom: 30px;
        cursor: pointer;

        .iconfont {
            font-size: 22px;
            color: var(--text2);
        }

        .liked {
            color: var(--link);
        }

        .collected {
            color: var(--pink);
        }
    }
}
</style>