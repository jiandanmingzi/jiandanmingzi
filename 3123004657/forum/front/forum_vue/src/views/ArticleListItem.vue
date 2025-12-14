<script setup>
import { ref, watch } from 'vue';
import { getCurrentInstance } from 'vue';
import { useStore } from 'vuex';
const store = useStore();
const { proxy } = getCurrentInstance();
const account = ref();
const props = defineProps({
    data: {
        type: Object
    },
    showIsTop: {
        type: Boolean,
        default: false
    }
})

const printProps = () => {
    console.log("ArticleListItem props:", props.data);
}
printProps();

const api = {
    handleDeletePost: '/posts/id',
}

const boardName = ref('');

const loadBoardName = () => {
    let boardList = store.state.boardList || [];
    let board = boardList.find(item => item.boardId == props.data.category);
    if (board) {
        boardName.value = board.boardName;
    }
}
loadBoardName();

const getAccount = () => {
    if (store.state.loginUserInfo && store.state.loginUserInfo.data) {
        account.value = store.state.loginUserInfo.data.account.toString();
        console.log("当前登录用户account：", account.value);
    }
}

watch(
    () => store.state.loginUserInfo,
    () => {
        getAccount();
    },
    { immediate: true, deep: true }
);

const deletePost = async () => {
    proxy.$confirm('确认要删除这篇文章吗？', '提示', {
        type: 'warning',
    }).then(async () => {
        const result = await proxy.Request({
            url: api.handleDeletePost,
            method: 'DELETE',
            dataType: "json",
            params: {
                post_id: props.data.post_id,
            },
        });
        if (result) {
            proxy.$message.success('删除成功');
            proxy.$router.go(0);
        }
    }).catch(() => { });
}
</script>

<template>
    <div class="article-item">
        <div class="article-item-inner">
            <div class="article-body">
                <div class="user-info">
                    <Avatar :userId="data.account" :width="30"></Avatar>
                    <router-link v-if="data.account" :to="'/ucenter/' + data.account" class="link-info">
                        {{ data.username }}
                    </router-link>
                    <div v-else class="anonymous-user">
                        {{ data.username }}
                    </div>
                    <el-divider direction="vertical"></el-divider>
                    <div class="post-time">{{ data.created_at }}</div>
                    <el-divider direction="vertical"></el-divider>
                    <router-link :to="`/forum/${data.category}`" class="link-info">
                        {{ boardName }}
                    </router-link>
                </div>
                <router-link :to="`/post/${data.post_id}`" class="article-title">
                    <span v-if="showIsTop && data.is_top == 1" class="top">
                        置顶
                    </span>
                    <span class="title">{{ data.title }}</span>
                </router-link>
                <div class="summary">{{ data.summary }}</div>
                <div class="article-footer">
                    <div class="article-info">
                        <span class="iconfont icon-eye-solid">
                            {{ data.view_count == 0 ? '阅读' : data.view_count }}
                        </span>
                        <span class="iconfont icon-good">
                            {{ data.like_count == 0 ? '点赞' : data.like_count }}
                        </span>
                        <span class="iconfont icon-comment">
                            {{ data.comment_count == 0 ? '评论' : data.comment_count }}
                        </span>
                    </div>
                    <div class="delete" v-if="data.account && account == data.account">
                        <span class="iconfont icon-del" @click="deletePost">
                            删除
                        </span>
                    </div>
                </div>
            </div>
        </div>
    </div>
</template>

<style scoped lang="scss">
.article-item {
    padding: 5px 15px 0 15px;

    .article-item-inner {
        border-bottom: 1px solid#ddd;
        padding: 10px;

        .article-body {
            .user-info {
                display: flex;
                align-items: center;
                font-size: 14px;
                color: #4e5969;

                .link-info {
                    margin-left: 5px;
                    text-decoration: none;
                    color: #4e5969;
                }

                .anonymous-user {
                    margin-left: 5px;
                    color: #4e5969;
                }

                .link-info:hover {
                    color: #409eff;
                }

                .post-time {
                    font-size: 13px;
                }
            }

            .article-title {
                font-weight: bold;
                color: #4a4a4a;
                text-decoration: none;
                font-size: 16px;
                margin: 10px 0;
                display: inline-block;

                .top {
                    font-size: 12px;
                    border-radius: 5px;
                    border: 1px solid var(--pink);
                    color: var(--pink);
                    padding: 0px 5px;
                    margin-right: 10px;
                }
            }

            .summary {
                font-size: 14px;
                color: #86909c;
            }

            .article-footer {
                display: flex;
                justify-content: space-between;
                align-items: center;

                .article-info {
                    margin-top: 10px;
                    display: flex;
                    align-items: center;
                    font-size: 13px;

                    .iconfont {
                        color: #86909c;
                        margin-right: 25px;
                        font-size: 14px;
                    }

                    .iconfont::before {
                        padding-right: 3px;
                    }
                }

                .delete {
                    font-size: 13px;
                    color: #E93323;
                    cursor: pointer;
                }
            }
        }
    }
}

.article-item:hover {
    background: #fafafa;
}
</style>