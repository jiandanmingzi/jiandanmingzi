<script setup>
import { ref, reactive, getCurrentInstance } from 'vue';
import { useRouter, useRoute } from 'vue-router';
import { watch } from 'vue';
import { useStore } from 'vuex';
import CommentList from './CommentList.vue';
const {proxy} = getCurrentInstance();
const route = useRoute();
const store = useStore();

const articleInfo = ref({});
const liked = ref(false);
const collected = ref(false);

const api = {
    getArticleDetail: '/api/posts/id',
    doLike: '/api/posts/id/like',
    doCollect: '/api/posts/id/collect',
}

const getArticleDetail = async (articleId) => {
    /*
    let result = await proxy.Request({
        url:api.getArticleDetail,
        dataType: "json",
        params: {
            articleId: articleId,
        },
    });

    if(!result){
        return;
    }
    articleInfo.value = result.data;
    liked.value = result.data.liked;
    collected.value = result.data.collected;
    store.commit("saveBoardId", result.data.categoryId);
    */
    articleInfo.value = {
        id: articleId,
        title: "示例文章标题",
        userId: "12345",
        nickName: "示例用户",
        createTime: "2024-01-01 12:00",
        readCount: 256,
        goodCount: 34,
        commentCount: 12,
        collectCount: 5,
        content: "<p>这是文章的示例内容。</p><p>可以包含多段文字和HTML标签。</p>",
        liked: false,
        collected: false,
    };
};

watch(
    () => route.params,
    (newVal, oldVal) => {
        if (newVal.articleId) {
            getArticleDetail(newVal.articleId);
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
    /*
    if (!store.getters.getLoginUserInfo) {
        proxy.$message.warning("请先登录！");
        store.commit("showLoginDialog", true);
        return;
    }
    let result = await proxy.Request({
        url: api.doLike,
        dataType: "json",
        params: {
            articleId: articleInfo.value.id,
        },
        method: 'post',
    });

    if(!result){
        return;
    }
    if (result.data.status == 'success') {
        liked.value = !liked.value;
        if (liked.value) {
            articleInfo.value.goodCount += 1;
        } else {
            articleInfo.value.goodCount -= 1;
        }
    }
        */
    if (liked.value){
        articleInfo.value.goodCount -= 1;
    } else {
        articleInfo.value.goodCount += 1;
    }
    liked.value = !liked.value;
};

const doCollectHandler = async () => {
    /*
    let result = await proxy.Request({
        url: api.doCollect,
        dataType: "json",
        params: {
            articleId: articleInfo.value.id,
        },
        method: 'post',
    });

    if(!result){
        return;
    }
    if (result.data.status == 'success') {
        collected.value = !collected.value;
        if (collected.value) {
            articleInfo.value.collectCount += 1;
        } else {
            articleInfo.value.collectCount -= 1;
        }
    }
        */
    if (collected.value){
        articleInfo.value.collectCount -= 1;
    } else {
        articleInfo.value.collectCount += 1;
    }
    collected.value = !collected.value;
};
</script>

<template>
<div 
    class="body-container article-list-body"
    :style="{ width: proxy.globalInfo.bodyWidth + 'px' }"
    >
    <div 
        class="detail-container"
        :style="{ width: proxy.globalInfo.bodyWidth - 300 + 'px' }"
        >
        <div class="article-detail">
            <div class="title">{{articleInfo.title}}</div>
            <div class="user-info">
                <Avatar :userId="articleInfo.userId"></Avatar>
                <div class="article-user-info">
                    <router-link class="nick-name" :to="`/ucenter/${articleInfo.userId}`">{{articleInfo.nickName}}</router-link>
                    <div class="article-info">
                        <span>{{articleInfo.createTime}}</span>
                        <span class="iconfont icon-eye-solid">
                            {{ articleInfo.readCount == 0 ? "阅读" : articleInfo.readCount }}
                        </span>
                    </div>
                </div>
            </div>
            <div class="detail" id="detail" v-html="articleInfo.content"></div>
        </div>
        <div class="comment-panel" id="view-comment">
            <CommentList
                v-if="articleInfo.id"
                :articleId="articleInfo.id"
                :articleUserId="articleInfo.userId"
            >
            </CommentList>
        </div>
    </div>
</div>
<div class="quick-panel" :style="{ left: quickPanelLeft + 'px'}">
    <el-badge 
        :value="articleInfo.goodCount"
        type="info"
        :hidden="!articleInfo.goodCount > 0"
        >
        <div class="quick-item" @click="doLikeHandler">
            <span :class="['iconfont icon-good', liked ? 'liked' : '']"></span>
        </div>
    </el-badge>
    <el-badge 
        :value="articleInfo.commentCount"
        type="info"
        :hidden="!articleInfo.commentCount > 0"
        >
        <div class="quick-item" @click="goToPosition('view-comment')">
            <span class="iconfont icon-comment"></span>
        </div>
    </el-badge>
    <el-badge 
        :value="articleInfo.collectCount"
        type="info"
        :hidden="!articleInfo.collectCount > 0"
        >
        <div class="quick-item" @click="doCollectHandler">
            <span :class="['iconfont icon-eye-solid', collected ? 'collected' : '']"></span>
        </div>
    </el-badge>
</div>
</template>

<style scoped lang="scss">
.body-container{
    .detail-container{
        .article-detail{
            padding: 15px;
            background: #fff;
            .title{
                font-weight: bolder;
            }
            .user-info{
                margin-top: 15px;
                display: flex;
                padding-bottom: 10px;
                border-bottom: 1px solid #ddd;
                .article-user-info{
                    margin-left: 10px;
                    .nick-name{
                        text-decoration: none;
                        color: #4e5969;
                        font-size: 15px;
                    }
                    .nick-name:hover{
                        color: var(--link);
                    }
                    .article-info{
                        margin-top: 5px;
                        font-size: 13px;
                        color: var(--text2);
                    }
                    .iconfont{
                        margin-left: 10px;
                    }
                    .iconfont::before{
                        padding-right: 3px;
                    }
                }
            }
            .detail{
                letter-spacing: 1px;
                line-height: 15px;
            }
        }
        .comment-panel{
            margin-top:20px;
            background: #fff;
        }
        
    }
}
.quick-panel{
    position: absolute;
    width: 50px;
    top: 150px;
    text-align: center;
    .el-badge__content.is-fixed{
        top: 5px;
        right: 15px;
    }
    .quick-item{
        width: 50px;
        height: 50px;
        display: flex;
        justify-content: center;
        align-items: center;
        border-radius: 50%;
        background: #fff;
        margin-bottom: 30px;
        cursor: pointer;
        .iconfont{
            font-size: 22px;
            color: var(--text2);
        }
        .liked{
            color: var(--link);
        }
        .collected{
            color: var(--pink);
        }
    }
}
</style>