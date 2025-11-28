<script setup>
import ArticleListItem from '@/views/ArticleListItem.vue';
import { ref, reactive, getCurrentInstance } from 'vue';
import { useRouter, useRoute } from 'vue-router';
import { watch } from 'vue';
import { useStore } from 'vuex';
import { dataType } from 'element-plus/es/components/table-v2/src/common';
const {proxy} = getCurrentInstance();
const router = useRouter();
const route = useRoute();
const store = useStore();

const api = {
    loadArticle: '/api/posts',
}
const boardId = ref(0);
const yearId = ref(0);
const roleId = ref(0);
const orderType = ref(0);
const loading = ref(false);
const articleListInfo = ref({});
const yearList = ref([]);
const roleList = ref([]);

const setYearList = () => {
    yearList.value = store.getters.getYearList;
};
setYearList();
        
const setRoleList = () => {
    roleList.value = store.getters.getRoleList;
};
setRoleList();

const changOrderType = (type)=>{
    orderType.value = type;
    loadArticle();
}

const loadArticle = async() => {
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
};

const saveYearId = (yearIdValue) => {
    store.commit('saveYearId', yearIdValue);
    yearId.value = yearIdValue;
};

const saveRoleId = (roleIdValue) => {
    store.commit('saveRoleId', roleIdValue);
    roleId.value = roleIdValue;
};

loadArticle();

watch(
    () => store.state,
    (newVal,  oldVal) => {
        yearId.value = newVal.yearId || 0;
        roleId.value = newVal.roleId || 0;
        loadArticle();
    },
    { immediate: true , deep: true}
)

watch(
    () => route.params,
    (newVal, oldVal) => {
        if (newVal.boardId) {
            boardId.value = newVal.boardId;
        } else {
            boardId.value = 0;
        }
        saveYearId(0);
        saveRoleId(0);
        loadArticle();
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
            <span :class="['year-role-item', roleId == 0 ? 'active' : '']" @click="saveRoleId(0)">
                全部
            </span>
            <span v-for="item in roleList" :class="['year-role-item', item.roleId == roleId ? 'active' : '']" @click="saveRoleId(item.roleId)">
                {{ item.roleName }}
            </span>
            <div class="divider"></div>
            <span :class="['year-role-item', yearId == 0 ? 'active' : '']" @click="saveYearId(0)">
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
            padding: 10px;
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