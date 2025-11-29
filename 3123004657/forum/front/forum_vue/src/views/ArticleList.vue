<script setup>
import ArticleListItem from '@/views/ArticleListItem.vue';
import { ref, reactive, getCurrentInstance } from 'vue';
import { useRouter, useRoute } from 'vue-router';
import { watch } from 'vue';
import { useStore } from 'vuex';
const {proxy} = getCurrentInstance();
const route = useRoute();
const store = useStore();

const api = {
    loadArticle: '/api/posts',
}
const boardId = ref('0');
const yearId = ref('0');
const roleId = ref('0');
const orderType = ref(0);
const loading = ref(false);
const articleListInfo = ref({});
const yearList = ref([]);
const roleList = ref([]);

const getYearList = () => {
    yearList.value = store.getters.getYearList;
};
getYearList();
        
const getRoleList = () => {
    roleList.value = store.getters.getRoleList;
};
getRoleList();

const changOrderType = (type)=>{
    orderType.value = type;
    loadArticle("order");
}

const loadArticle = async (reason) => {
    /*
    loading.value = true;
    let params = {
        pageNo: articleListInfo.value.pageNo || 1,
        orderType: orderType.value,
    };
    if (boardId.value != 0) {
        params.category = boardId.value;
    }
    if (yearId.value != 0) {
            params.grade = yearId.value;
    }
    if (roleId.value != 0) {
            params.role = roleId.value;
    }
    let result = await proxy.Request({
        url: api.loadArticle,
        dataType: "json",
        params: params,
    });
    loading.value = false;
    if (!result) {
        return;
    }
    articleListInfo.value = result.data;
    */
    console.log("Loading articles due to:", reason);
    //本地示例
    console.log("Loading articles locally");
    articleListInfo.value = {
        pageNo: 1,
        pageSize: 10,
        totalCount: 2,
        totalPage: 1,
        dataList: [
            {
                articleId: "1",
                userId: "12345",
                nickName: "示例用户",
                postTime: "2024-01-01 12:00",
                boardId: "campus_life",
                boardName: "校园生活",
                topType: 0,
                title: "校园活动回顾",
                summary: "上周我们举办了一场精彩的校园活动，快来看看吧！",
                readCount: 256,
                goodCount: 34,
                commentCount: 12,
            },
            {
                articleId: "2",
                userId: "u2",
                nickName: "用户二",
                postTime: "2024-01-02 11:00",
                boardId: "campus_life",
                boardName: "校园生活",
                topType: 0,
                title: "校园活动回顾",
                summary: "上周我们举办了一场精彩的校园活动，快来看看吧！",
                readCount: 80,
                goodCount: 15,
                commentCount: 5,
            },
        ],
    };
};

const saveBoardId = (boardIdValue) => {
    boardId.value = boardIdValue;
    store.commit('saveBoardId', boardIdValue);
};

const saveYearId = (yearIdValue) => {
    yearId.value = yearIdValue;
    store.commit('saveYearId', yearIdValue);
};

const setYearId_0 = () => {
    yearId.value = '0';
    store.commit('saveYearId', '0');
    loadArticle("yearId");
};

const saveRoleId = (roleIdValue) => {
    roleId.value = roleIdValue;
    store.commit('saveRoleId', roleIdValue);
};

const setRoleId_0 = () => {
    roleId.value = '0';
    store.commit('saveRoleId', '0');
    loadArticle("roleId");
};

watch(
    () => store.state.yearId,
    (newVal,  oldVal) => {
        if (oldVal != undefined && oldVal != newVal){
            yearId.value = newVal;
            console.log("yearId changed:", yearId.value);
            if (yearId.value != '0'){
                loadArticle("yearId");
            }
        }
    },
    { immediate: true , deep: true}
)

watch(
    () => store.state.roleId,
    (newVal,  oldVal) => {
            if (oldVal != undefined && oldVal != newVal){
            roleId.value = newVal;
            console.log("roleId changed:", roleId.value);
            if (roleId.value != '0'){
                loadArticle("roleId");
            }
        }
    },
    { immediate: true , deep: true}
)

watch(
    () => route.params,
    (newVal, oldVal) => {
        if (newVal.boardId) {
            saveBoardId(newVal.boardId);
        } else {
            saveBoardId('0');
        }
        saveYearId('0');
        saveRoleId('0');
        orderType.value = 0;
        loadArticle("route");
    },
    { immediate: true , deep: true}
)
</script>

<template>
    <div 
    class="body-container article-list-body"
    :style="{ width: proxy.globalInfo.bodyWidth + 'px' }"
    >
        <div class="sub-board">
            <span :class="['year-role-item', roleId == '0' ? 'active' : '']" @click="setRoleId_0()">
                全部
            </span>
            <span v-for="item in roleList" :class="['year-role-item', item.roleId == roleId ? 'active' : '']" @click="saveRoleId(item.roleId)">
                {{ item.roleName }}
            </span>
            <div class="divider"></div>
            <span :class="['year-role-item', yearId == '0' ? 'active' : '']" @click="setYearId_0()">
                全部
            </span>
            <span v-for="item in yearList" :class="['year-role-item', item.yearId == yearId ? 'active' : '']" @click="saveYearId(item.yearId)">
                {{ item.yearName }}
            </span>
        </div>
        <div class="article-panel">
            <div class="top-tab">
                <div 
                :class="['tab', orderType == 0 ? 'active' : '']" 
                @click="changOrderType(0)"
                >
                    最热
                </div>
                <el-divider direction="vertical"></el-divider>
                <div 
                :class="['tab', orderType == 1 ? 'active' : '']" 
                @click="changOrderType(1)"
                >
                    发布时间
                </div>
                <el-divider direction="vertical"></el-divider>
                <div 
                :class="['tab', orderType == 2 ? 'active' : '']" 
                @click="changOrderType(2)"
                >
                    最新发布
                </div>
                <el-divider direction="vertical"></el-divider>
                <div 
                :class="['tab', orderType == 3 ? 'active' : '']" 
                @click="changOrderType(3)"
                >
                    最近回复
                </div>
            </div>
            <div class="article-list">
                <DataList 
                :loading="loading" 
                :dataSource="articleListInfo" 
                @loadData="loadArticle"
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
.article-list-body{
    .sub-board{
        padding: 10px 0px;
        .divider {
            height: 1px;
            background-color: #e8e8e8;
            margin: 5px 0;
        }
        .year-role-item{
            background: #fff;
            border-radius: 15px;
            padding: 2px 10px;
            margin-right: 10px;
            color: #909090;
            cursor: pointer;
            font-size: 14px;
            a {
                text-decoration: none;
                color: #909090;
            }
        }
        .active{
            background: var(--link);
            color: #fff;
            a {
                color: #fff;
            }
        }
    }
    .article-panel{
        background: #fff;
        .top-tab{
            display: flex;
            align-items: center;
            padding: 10px 15px;
            font-size: 15px;
            border-bottom: 1px solid #ddd;
            .tab{
                cursor: pointer;
            }
            .active{
                color: var(--link);
            }
        }
    }
}
</style>