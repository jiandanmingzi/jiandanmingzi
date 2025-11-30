<script setup>
import { ref, reactive, getCurrentInstance, watch } from 'vue';
import { useStore } from 'vuex';

const { proxy } = getCurrentInstance();
const store = useStore();

const props = defineProps({
    articleId: {
        type: String,
        required: true,
    },
    articleUserId: {
        type: String,
        required: true,
    }
});

const api = {
    loadComment: "/comment/loadComment",
    postComment: "/comment/postComment",
    doLike: "/comment/doLike",
    loadSubComment: "/comment/loadSubComment", 
};

// 评论列表数据
const commentListInfo = ref({});
const orderType = ref(0); // 0:热榜 1:最新
const commentContent = ref("");

const indexCommentReply = () => {
    for (let comment of commentListInfo.value.list) {
        comment.showSubPagination = false;
        comment.showReply = false;
        comment.replyContent = "";
    }
}

//本地模拟数据-----------------------------------------------------------------------

// 生成子评论
const mockSubCommentsService = (totalCount) => {
    const pageSize = 10;
    const totalPage = Math.ceil(totalCount / pageSize);
    
    const list = [];
    // 模拟后端逻辑：返回 (x-1)*10+1 到 x*10 条
    const start = 1;
    const end = totalCount;

    for (let k = start; k <= end; k++) {
        let pageNo = Math.ceil(k / pageSize);
        list.push({
            commentId: `sub_${k}`,
            userId: `subUser${k}`,
            nickName: `子用户${k}`,
            content: `这是第 ${pageNo} 页的第 ${k} 条评论内容。`,
            postTime: "2023-08-03 10:00",
            goodCount: k,
            replyUserId: k % 3 === 0 ? `subUser${k-1}` : null, // 模拟部分回复
            replyNickName: k % 3 === 0 ? `子用户${k-1}` : null,
            liked: false,
        });
    }

    return {
        dataList: list,
        pageNo: 1,
        pageTotal: totalPage,
        totalCount: totalCount
    };
};

const getSubComments = (parentCommentId, pageNo) => {
    const pageSize = 10;
    
    for (let comment of commentListInfo.value.list) {
        if (comment.commentId === parentCommentId) {
            const allSubComments = comment.subCommentInfo.allDataList || comment.subCommentInfo.dataList;
            const startIndex = (pageNo - 1) * pageSize;
            const endIndex = startIndex + pageSize;
            
            return {
                dataList: allSubComments.slice(startIndex, endIndex),
                pageNo: pageNo,
                pageTotal: comment.subCommentInfo.pageTotal,
                totalCount: comment.subCommentInfo.totalCount
            };
        }
    }
    return {
        dataList: [],
        pageNo: 1,
        pageTotal: 0,
        totalCount: 0
    };
};

// 生成本地测试数据
const getMockComments = () => {
    const list = [];
    for (let i = 1; i <= 5; i++) {
        // 只有第一条评论有大量子评论用于测试分页
        const hasSub = i === 1; 
        
        let subCommentInfo = {
            dataList: [],
            pageNo: 1,
            pageTotal: 0,
            totalCount: 0
        };

        if (hasSub) {
            subCommentInfo = mockSubCommentsService(32);
        }

        list.push({
            articleId: props.articleId,
            commentId: `${i}`,
            userId: i === 1 ? props.articleUserId : `user${i}`,
            nickName: i === 1 ? "程序员" : `路人甲${i}`,
            avatar: "", 
            content: i === 1 ? "沙发自己做" : "测试评论内容",
            postTime: "2023-01-16 20:54",
            goodCount: i === 1 ? 14 : 3,
            // 子评论数据结构调整，适应DataList
            subCommentInfo: subCommentInfo,
            liked: false, // 是否点赞
        });
    }
    return {
        list: list,
        totalCount: 44,
    };
};

const indexComment = () => {
    commentListInfo.value = getMockComments();
}
indexComment();

//---------------------------------------------------------------------------------


const loadComment = async () => {
    // 模拟API请求
    console.log("Loading comments...");

    /* let result = await proxy.Request({
        url: api.loadComment,
        params: {
            articleId: props.articleId,
            orderType: orderType.value,
        }
    }); 
    */


    indexCommentReply();
};

loadComment();

// 分页加载子评论
const loadSubComment = /*async*/ (item) => {
    console.log(`加载评论 ${item.commentId} 的第 ${item.subCommentInfo.pageNo} 页子评论`);
    
    // 模拟API请求
    // let result = await proxy.Request({
    //     url: api.loadSubComment,
    //     params: {
    //         parentCommentId: item.commentId,
    //         pageNo: pageNo
    //     }
    // })
    
    // 本地模拟数据返回
    // 本地模拟数据返回
    const result = getSubComments(item.commentId, item.subCommentInfo.pageNo);
    
    // 更新当前评论的子评论列表
    item.subCommentInfo.dataList = result.dataList;
    item.subCommentInfo.pageNo = result.pageNo; // 确保页码同步
    item.subCommentInfo.pageTotal = result.pageTotal;
    item.subCommentInfo.totalCount = result.totalCount;
};

const postCommentHandler = /*async*/() => {
    if (!commentContent.value) {
        proxy.$message.warning("请输入评论内容");
        return;
    }

    /*
    let result = await proxy.Request({
        url: api.postComment,
        dataType: "json",
        params: {
            articleId: props.articleId,
            content: commentContent.value,
        },
        method: 'post',
    });
    if(!result){
        return;
    }
    if (result.data.status == 'success') {
        proxy.$message.success("评论成功");
        commentContent.value = "";
        loadComment();
    }
    */

    // 模拟提交
    commentListInfo.value.list.unshift({
        commentId: "new_" + Date.now(),
        userId: store.getters.getLoginUserInfo?.userId || "me",
        nickName: store.getters.getLoginUserInfo?.nickName || "我",
        content: commentContent.value,
        postTime: "刚刚",
        goodCount: 0,
        subCommentInfo: { dataList: [], pageNo: 1, pageTotal: 0, totalCount: 0 },
    });
    commentContent.value = "";
    proxy.$message.success("评论成功");
    loadComment();
};

const postSubCommentHandler = /*async*/(item) => {
    /*
    let result = await proxy.Request({
        url: api.postComment,
        dataType: "json",
        params: {
            articleId: item.articleId,
            content: item.replyContent,
            parentCommentId: item.commentId,
        },
        method: 'post',
    });
    if(!result){
        return;
    }
    if (result.data.status == 'success') {
        proxy.$message.success("子评论提交成功");
        loadComment();
    }
    */

    // 模拟提交
    item.subCommentInfo.dataList.unshift({
        commentId: "sub_new_" + Date.now(),
        userId: store.getters.getLoginUserInfo?.userId || "me",
        nickName: store.getters.getLoginUserInfo?.nickName || "我",
        content: item.replyContent,
        postTime: "刚刚",
        goodCount: 0,
        liked: false,
    });
    item.replyContent = "";

    proxy.$message.success("子评论提交成功");
    loadComment();
};

const doLike = /*async*/ (item) => {
    /*
    let result = await proxy.Request({
        url: api.doLike,
        dataType: "json",
        params: {
            commentId: item.commentId,
        },
        method: 'post',
    });
    if(!result){
        return;
    }
    if (result.data.status == 'success') {
        item.liked = !item.liked;
        if (item.liked){
            item.goodCount += 1;
            proxy.$message.success("点赞成功");
        } else {
            item.goodCount -= 1;
            proxy.$message.success("取消点赞");
        }
    }
    */
    if (item.liked){
        item.goodCount -= 1;
        proxy.$message.success("取消点赞");
    } else {
        item.goodCount += 1;
        proxy.$message.success("点赞成功");
    }
    item.liked = !item.liked;
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
            <div class="title">评论 <span class="count">{{ commentListInfo.totalCount }}</span></div>
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
                <el-input
                    v-model="commentContent"
                    type="textarea"
                    :rows="2"
                    placeholder="请文明发言"
                    maxlength="800"
                    show-word-limit
                    resize="none"
                ></el-input>
                <el-button type="primary" class="post-btn" @click="postCommentHandler">发表</el-button>
            </div>
        </div>

        <!-- 评论列表 -->
        <div class="comment-list">
            <div class="comment-item" v-for="item in commentListInfo.list" :key="item.commentId">
                <div class="avatar-box">
                    <Avatar :width="40" :userId="item.userId"></Avatar>
                </div>
                <div class="comment-content">
                    <div class="nick-name">
                        <span class="name">{{ item.nickName }}</span>
                        <span v-if="item.userId === articleUserId" class="author-tag">作者</span>
                    </div>
                    <div class="comment-text" v-html="item.content"></div>
                    
                    <!-- 评论底部信息 -->
                    <div class="comment-info">
                        <span class="time">{{ item.postTime }}</span>
                        <span :class="['like-icon', item.liked ? 'liked' : '']" @click="doLike(item)">
                            <span class="iconfont icon-good"></span>
                            {{ item.goodCount > 0 ? item.goodCount : '点赞' }}
                        </span>
                        <span :class="['reply-btn', item.showReply ? 'active' : '']" @click="showReplyPanel(item)">
                            <span class="iconfont icon-comment"></span>
                            回复
                        </span>
                    </div>

                    <!-- 子评论列表 -->
                    <div class="sub-comment-list" v-if="item.subCommentInfo && item.subCommentInfo.dataList.length > 0">
                        
                        <!-- 未展开状态：只显示前3条 -->
                        <template v-if="!item.showSubPagination">
                            <div class="sub-comment-item" v-for="sub in item.subCommentInfo.dataList.slice(0, 3)" :key="sub.commentId">
                                <div class="avatar-box">
                                    <Avatar :width="30" :userId="sub.userId"></Avatar>
                                </div>
                                <div class="sub-content-info">
                                    <div class="nick-name">
                                        <span class="name">{{ sub.nickName }}</span>
                                        <span v-if="sub.userId === articleUserId" class="author-tag">作者</span>
                                        <span class="reply-text" v-if="sub.replyUserId"> 回复 <span class="name">@{{ sub.replyNickName }}</span></span>
                                        <span>：</span>
                                        <span class="sub-text" v-html="sub.content"></span>
                                    </div>
                                    <div class="comment-info">
                                        <span class="time">{{ sub.postTime }}</span>
                                        <span :class="['like-icon', sub.liked ? 'liked' : '']" @click="doLike(sub)">
                                            <span class="iconfont icon-good"></span>
                                            {{ sub.goodCount > 0 ? sub.goodCount : '点赞' }}
                                        </span>
                                    </div>
                                </div>
                            </div>
                            <!-- 展开按钮 -->
                            <div class="more-comment" v-if="item.subCommentInfo.totalCount > 3">
                                <span @click="item.showSubPagination = true">
                                    共{{ item.subCommentInfo.totalCount }}条回复，点击查看
                                </span>
                            </div>
                        </template>

                        <!-- 展开状态：使用DataList显示分页 -->
                        <template v-else>
                            <DataList 
                                :dataSource="item.subCommentInfo" 
                                @loadData="loadSubComment(item)"
                            >
                                <template #default="{data}">
                                    <div class="sub-comment-item">
                                        <div class="avatar-box">
                                            <Avatar :width="30" :userId="data.userId"></Avatar>
                                        </div>
                                        <div class="sub-content-info">
                                            <div class="nick-name">
                                                <span class="name">{{ data.nickName }}</span>
                                                <span v-if="data.userId === articleUserId" class="author-tag">作者</span>
                                                <span class="reply-text" v-if="data.replyUserId"> 回复 <span class="name">@{{ data.replyNickName }}</span></span>
                                                <span>：</span>
                                                <span class="sub-text" v-html="data.content"></span>
                                            </div>
                                            <div class="comment-info">
                                                <span class="time">{{ data.postTime }}</span>
                                                <span class="address">&nbsp;·&nbsp;{{ data.userIpAddress }}</span>
                                                <span :class="['like-icon', data.liked ? 'liked' : '']" @click="doLike(data)">
                                                    <span class="iconfont icon-good"></span>
                                                    {{ data.goodCount > 0 ? data.goodCount : '点赞' }}
                                                </span>
                                            </div>
                                        </div>
                                    </div>
                                </template>
                            </DataList>
                            <!-- 收起按钮 -->
                            <div class="more-comment">
                                <span @click="item.showSubPagination = false">
                                    收起回复
                                </span>
                            </div>
                        </template>
                    </div>

                    <!-- 回复输入框 -->
                    <div class="reply-input-panel" v-if="item.showReply">
                         <el-input 
                         placeholder="回复..." 
                         v-model="item.replyContent"
                         type="textarea"
                        :rows="2"
                        maxlength="800"
                        show-word-limit
                        resize="none"
                         ></el-input>
                         <div class="reply-btn-box">
                             <el-button type="primary" size="small" @click="postSubCommentHandler(item)">回复</el-button>
                         </div>
                    </div>
                </div>
            </div>
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
            border-bottom: 1px solid #f0f0f0;
            padding-bottom: 20px;
            &:last-child {
                border-bottom: none;
            }
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
                    .time, .address {
                        margin-right: 15px;
                    }
                    .like-icon, .reply-btn {
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