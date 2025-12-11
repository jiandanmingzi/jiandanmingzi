<script setup>
import { ref, reactive, getCurrentInstance, watch } from 'vue';
import { useStore } from 'vuex';

const { proxy } = getCurrentInstance();
const store = useStore();
const is_anonymous = ref(false);

const props = defineProps({
    articleId: {
        type: String,
        required: true,
    },
    articleUserId: {
        type: String,
        required: true,
    },
    totalCount: {
        type: Number,
        default: 0,
    },
});

const api = {
    handleGetParentComments: "/comments/parent",
    handleGetChildComments: "/comments/child",
    handleGetParentCommentsByLikes: "/comments/parent/likes",
    handleCreateComment: "/posts/post_id/comments",
    handleToggleCommentLike: "/comments/id/like",
    loadSubComment: "/comment/loadSubComment",
    handleIsCommentLiked: "/comments/id/is-liked",
    handleGetChildCommentCount: "/comments/child/count",
};

// 评论列表数据
const commentListInfo = ref({
    page: 1,
    page_size: 10,
    count: props.totalCount,
    data: [],
});
const orderType = ref(0); // 0:热门 1:最新
const commentContent = ref("");

const isLiked = async (comment_id) => {
    let result = await proxy.Request({
        url: api.handleIsCommentLiked,
        dataType: "json",
        params: {
            comment_id: comment_id,
        },
    });

    if (!result) {
        return false;
    }
    return result.is_liked;
}

const getChildCommentCount = async (parent_id) => {
    let result = await proxy.Request({
        url: api.handleGetChildCommentCount,
        dataType: "json",
        params: {
            parent_id: parent_id,
        },
    });

    if (!result) {
        return 0;
    }
    return result.count;
};

// 分页加载子评论
const loadSubComment = async (item) => {
    let result = await proxy.Request({
        url: api.handleGetChildComments,
        dataType: "json",
        params: {
            parent_id: item.comment_id,
            page: item.subCommentInfo.page || 1,
            page_size: item.subCommentInfo.page_size || 10,
        },
    });

    if (!result) {
        return;
    }
    item.subCommentInfo.data = result.data;
    for (let subItem of item.subCommentInfo.data) {
        subItem.liked = await isLiked(subItem.comment_id);
    }
};

const loadComment = async () => {
    let result;
    if (orderType.value === 0) {
        // 热门评论
        result = await proxy.Request({
            url: api.handleGetParentCommentsByLikes,
            dataType: "json",
            params: {
                post_id: props.articleId,
                page: commentListInfo.value.page || 1,
                page_size: commentListInfo.value.page_size || 10,
            },
        });
    } else {
        // 最新评论
        result = await proxy.Request({
            url: api.handleGetParentComments,
            dataType: "json",
            params: {
                post_id: props.articleId,
                page: commentListInfo.value.page || 1,
                page_size: commentListInfo.value.page_size || 10,
            },
        });
    }
    if (!result) {
        return;
    }
    commentListInfo.value.data = result.data;
    for (let item of commentListInfo.value.data) {
        item.showReply = false;
        item.replyContent = "";
        item.showSubPagination = false;
        item.is_anonymous = false;
        item.liked = await isLiked(item.comment_id);
        item.subCommentInfo.total_count = await getChildCommentCount(item.comment_id);
        if (item.subCommentInfo.total_count > 0) {
            item.subCommentInfo.page_size = 10;
            item.subCommentInfo.page = 1;
            item.subCommentInfo.pageTotal = Math.ceil(item.subCommentInfo.total_count / item.subCommentInfo.page_size);
            loadSubComment(item);
        }
    }
};

const postCommentHandler = async (item = null) => {
    if (!commentContent.value) {
        proxy.$message.warning("请输入评论内容");
        return;
    }
    let result;
    if (item) {
        result = await proxy.Request({
            url: api.handleCreateComment,
            dataType: "json",
            params: {
                post_id: props.articleId,
                content: item.replyContent,
                parent_id: item.comment_id,
                is_anonymous: item.is_anonymous,
            },
            method: 'post',
        });
    } else {
        result = await proxy.Request({
            url: api.handleCreateComment,
            dataType: "json",
            params: {
                post_id: props.articleId,
                content: commentContent.value,
                is_anonymous: is_anonymous.value,
            },
            method: 'post',
        });
    }
    if (!result) {
        return;
    }
    commentContent.value = "";
    if (item) {
        item.replyContent = "";
    }
    proxy.$message.success("评论成功");
    loadComment();
};

const doLike = async (item) => {
    let result = await proxy.Request({
        url: api.handleToggleCommentLike,
        dataType: "json",
        params: {
            comment_id: item.comment_id,
        },
        method: 'post',
    });

    if (!result) {
        return;
    }
    if (result.data.status == 'success') {
        item.liked = !item.liked;
        if (item.liked) {
            item.goodCount += 1;
        } else {
            item.goodCount -= 1;
        }
    }
};

const showReplyPanel = (item) => {
    item.showReply = !item.showReply;
};

const changeOrder = (type) => {
    orderType.value = type;
    loadComment();
};
</script>


<template>
    <div class="comment-body">
        <!-- 顶部标题与Tab -->
        <div class="comment-title">
            <div class="title">评论 <span class="count">{{ totalCount }}</span></div>
            <div class="tab">
                <span :class="['tab-item', orderType === 0 ? 'active' : '']" @click="changeOrder(0)">热榜</span>
                <span class="divider">|</span>
                <span :class="['tab-item', orderType === 1 ? 'active' : '']" @click="changeOrder(1)">最新</span>
            </div>
        </div>

        <!-- 发送评论框 -->
        <div class="comment-form-panel">
            <div class="avatar-box">
                <Avatar :width="50" :userId="store.getters.getLoginUserInfo?.userId"></Avatar>
            </div>
            <div class="form-content">
                <el-input v-model="commentContent" type="textarea" :rows="2" placeholder="请文明发言" maxlength="800"
                    show-word-limit resize="none"></el-input>
                <el-button type="primary" class="post-btn" @click="postCommentHandler()">发表</el-button>
                <div class="anonymous-checkbox">
                    <el-checkbox v-model="is_anonymous">匿名发布</el-checkbox>
                </div>
            </div>
        </div>

        <!-- 评论列表 -->
        <div class="comment-list">
            <DataList :dataSource="commentListInfo" @loadData="loadComment()">
                <template #default="{ data }">
                    <div class="comment-item" :key="data.comment_id">
                        <div class="avatar-box">
                            <Avatar :width="40" :userId="data.account"></Avatar>
                        </div>
                        <div class="comment-content">
                            <div class="nick-name">
                                <span class="name">{{ data.username }}</span>
                                <span v-if="data.account === articleUserId" class="author-tag">作者</span>
                            </div>
                            <div class="comment-text" v-html="data.content"></div>

                            <!-- 评论底部信息 -->
                            <div class="comment-info">
                                <span class="time">{{ data.created_at }}</span>
                                <span :class="['like-icon', data.liked ? 'liked' : '']" @click="doLike(data)">
                                    <span class="iconfont icon-good"></span>
                                    {{ data.like_count > 0 ? data.like_count : '点赞' }}
                                </span>
                                <span :class="['reply-btn', data.showReply ? 'active' : '']"
                                    @click="showReplyPanel(data)">
                                    <span class="iconfont icon-comment"></span>
                                    回复
                                </span>
                            </div>

                            <!-- 子评论列表 -->
                            <div class="sub-comment-list"
                                v-if="data.subCommentInfo && data.subCommentInfo.data.length > 0">

                                <!-- 未展开状态：只显示前3条 -->
                                <template v-if="!data.showSubPagination">
                                    <div class="sub-comment-item" v-for="sub in data.subCommentInfo.data.slice(0, 3)"
                                        :key="sub.comment_id">
                                        <div class="avatar-box">
                                            <Avatar :width="30" :userId="sub.account"></Avatar>
                                        </div>
                                        <div class="sub-content-info">
                                            <div class="nick-name">
                                                <span class="name">{{ sub.username }}</span>
                                                <span v-if="sub.account === articleUserId" class="author-tag">作者</span>
                                                <span class="sub-text" v-html="sub.content"></span>
                                            </div>
                                            <div class="comment-info">
                                                <span class="time">{{ sub.created_at }}</span>
                                                <span :class="['like-icon', sub.liked ? 'liked' : '']"
                                                    @click="doLike(sub)">
                                                    <span class="iconfont icon-good"></span>
                                                    {{ sub.like_count > 0 ? sub.like_count : '点赞' }}
                                                </span>
                                            </div>
                                        </div>
                                    </div>
                                    <!-- 展开按钮 -->
                                    <div class="more-comment" v-if="data.subCommentInfo.totalCount > 3">
                                        <span @click="data.showSubPagination = true">
                                            共{{ data.subCommentInfo.totalCount }}条回复，点击查看
                                        </span>
                                    </div>
                                </template>

                                <!-- 展开状态：使用DataList显示分页 -->
                                <template v-else>
                                    <DataList :dataSource="data.subCommentInfo" @loadData="loadSubComment(data)">
                                        <template #default="{ data }">
                                            <div class="sub-comment-item">
                                                <div class="avatar-box">
                                                    <Avatar :width="30" :userId="data.account"></Avatar>
                                                </div>
                                                <div class="sub-content-info">
                                                    <div class="nick-name">
                                                        <span class="name">{{ data.username }}</span>
                                                        <span v-if="data.account === articleUserId"
                                                            class="author-tag">作者</span>
                                                        <span class="reply-text"> 回复: </span>
                                                        <span class="sub-text" v-html="data.content"></span>
                                                    </div>
                                                    <div class="comment-info">
                                                        <span class="time">{{ data.created_at }}</span>
                                                        <span :class="['like-icon', data.liked ? 'liked' : '']"
                                                            @click="doLike(data)">
                                                            <span class="iconfont icon-good"></span>
                                                            {{ data.like_count > 0 ? data.like_count : '点赞' }}
                                                        </span>
                                                    </div>
                                                </div>
                                            </div>
                                        </template>
                                    </DataList>
                                    <!-- 收起按钮 -->
                                    <div class="more-comment">
                                        <span @click="data.showSubPagination = false">
                                            收起回复
                                        </span>
                                    </div>
                                </template>
                            </div>

                            <!-- 回复输入框 -->
                            <div class="reply-input-panel" v-if="data.showReply">
                                <el-input placeholder="回复..." v-model="data.replyContent" type="textarea" :rows="2"
                                    maxlength="800" show-word-limit resize="none"></el-input>
                                <div class="reply-btn-box">
                                    <el-button type="primary" size="small"
                                        @click="postCommentHandler(item)">回复</el-button>
                                    <div class="anonymous-checkbox">
                                        <el-checkbox v-model="data.is_anonymous">匿名发布</el-checkbox>
                                    </div>
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
.comment-body {
    .comment-title {
        display: flex;
        align-items: center;
        margin-bottom: 10px;
        padding: 25px 0px 10px 20px;

        .title {
            font-size: 25px;
            font-weight: bold;

            .count {
                font-size: 15px;
            }
        }

        .tab {
            margin-top: 10px;
            margin-left: 10px;
            font-size: 15px;
            color: #909399;

            .tab-item {
                cursor: pointer;
                padding: 0 10px;
            }

            .active {
                color: var(--link);
            }

            .divider {
                margin: 0 5px;
            }
        }
    }

    .comment-form-panel {
        margin-left: 30px;
        display: flex;
        margin-bottom: 20px;

        .avatar-box {
            margin-right: 10px;
        }

        .form-content {
            margin-right: 30px;
            flex: 1;
            display: flex;
            gap: 10px;
            align-items: flex-start;

            .el-textarea {
                flex: 1;
            }

            .post-btn {
                float: none;
                width: 60px;
                height: 100%;
                white-space: nowrap;
            }
        }
    }

    .comment-list {
        .comment-item {
            margin-left: 40px;
            margin-right: 50px;
            display: flex;
            margin-bottom: 20px;
            border-bottom: 1px solid #ddd;
            padding-bottom: 20px;

            .avatar-box {
                margin-right: 15px;
            }

            .comment-content {
                flex: 1;

                .nick-name {
                    font-size: 14px;
                    color: #666;
                    margin-bottom: 5px;
                    display: flex;
                    align-items: center;

                    .name {
                        color: #666;
                        cursor: pointer;
                        font-weight: 500;

                        &:hover {
                            color: var(--link);
                        }
                    }

                    .author-tag {
                        background: var(--pink);
                        color: #fff;
                        font-size: 12px;
                        padding: 0 4px;
                        border-radius: 2px;
                        margin-left: 5px;
                        transform: scale(0.9);
                    }
                }

                .comment-text {
                    font-size: 15px;
                    color: #333;
                    line-height: 1.6;
                    margin-bottom: 8px;
                }

                .comment-info {
                    font-size: 13px;
                    color: #999;
                    display: flex;
                    align-items: center;

                    .time,
                    .address {
                        margin-right: 15px;
                    }

                    .like-icon,
                    .reply-btn {
                        cursor: pointer;
                        margin-right: 15px;
                        display: flex;
                        align-items: center;

                        &:hover {
                            color: var(--link);
                        }

                        .iconfont {
                            margin-right: 3px;
                            font-size: 14px;
                        }
                    }

                    .liked {
                        color: var(--link);
                    }
                }

                .sub-comment-list {
                    margin-top: 15px;
                    background: #f9f9f9;
                    padding: 15px;
                    border-radius: 5px;
                    margin-right: 200px;

                    .sub-comment-item {
                        display: flex;
                        margin-bottom: 15px;

                        &:last-child {
                            margin-bottom: 0;
                        }

                        .avatar-box {
                            margin-right: 10px;
                        }

                        .sub-content-info {
                            flex: 1;

                            .nick-name {
                                font-size: 14px;
                                margin-bottom: 3px;

                                .reply-text {
                                    color: #333;
                                    margin: 0 5px;
                                }

                                .sub-text {
                                    color: #333;
                                    font-size: 14px;
                                }
                            }
                        }
                    }

                    .more-comment {
                        margin-top: 10px;
                        font-size: 13px;
                        color: var(--link);
                        cursor: pointer;
                    }
                }

                .reply-input-panel {
                    margin-top: 10px;

                    .reply-btn-box {
                        text-align: right;
                        margin-top: 5px;
                    }
                }
            }
        }
    }
}
</style>