<script setup>
const props = defineProps({
    data:{
        type:Object
    },
})
</script>

<template>
<div class="article-item">
    <div class="article-item-inner">
        <div class="article-body">
            <div class="user-info">
                <Avatar 
                :userId="data.userId"
                :width="30"
                ></Avatar>
                <router-link :to="'/user/' + data.userId" class="link-info">
                    {{ data.nickName }}
                </router-link>
                <el-divider direction="vertical"></el-divider>
                <div class="post-time">{{ data.postTime }}</div>
                <el-divider direction="vertical"></el-divider>
                <router-link :to="'/board/'" class="link-info">
                    {{ data.boardName }}
                </router-link>
            </div>
            <router-link :to="'/article/'" class="article-title">
                {{ data.title }}
            </router-link>
            <div class="summary">{{ data.summary }}</div>
            <div class="article-info">
                <span class="iconfont icon-eye-solid">
                    {{ data.readCount == 0 ? '阅读' : data.readCount }}
                </span>
                <span class="iconfont icon-good">
                    {{ data.readCount == 0 ? '点赞' : data.goodCount }}
                </span>
                <span class="iconfont icon-comment">
                    {{ data.readCount == 0 ? '评论' : data.commentCount }}
                </span>
            </div>
        </div>
    </div>
</div>
</template>

<style scoped lang="scss">
.article-item{
    padding: 5px 10px 0 10px;
    .article-item-inner{
        border-bottom: 1px solid#ddd;
        padding: 10px;
        .article-body{
            .user-info{
                display: flex;
                align-items: center;
                font-size: 14px;
                color: #4e5969;
                .link-info{
                    margin-left: 5px;
                    text-decoration: none;
                }
                .link-info:hover{
                    color: #409eff;
                }
                .post-time{
                    font-size: 13px;
                }
            }
            .article-title{
                font-weight: bold;
                text-decoration: none;
                color: #4a4a4a;
                font-size: 16px;
                margin: 10px 0;
                display: inline-block
            }
            .summary{
                font-size: 14px;
                color: #86909c;
            }
            .article-info{
                margin-top: 10px;
                display: flex;
                align-items: center;
                font-size: 13px;
                .iconfont{
                    color: #86909c;
                    margin-right: 25px;
                    font-size: 14px;
                }
                .iconfont::before{
                    padding-right: 3px;
                }
            }
        }
    }
}
.article-item:hover{
    background: #fafafa;
}
</style>