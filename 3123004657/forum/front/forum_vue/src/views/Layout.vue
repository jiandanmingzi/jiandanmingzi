<script setup>
import { ref, reactive, getCurrentInstance, onMounted, watch } from 'vue';
import { useRouter, useRoute } from 'vue-router';
import Login from './Login.vue';
import { useStore } from 'vuex';
const store = useStore();
const {proxy} = getCurrentInstance();
const router = useRouter();
const route = useRoute();
const userInfo = ref({});
const showHeader = ref(true);
const loginRef = ref();

const login = (type) => {
    loginRef.value.showPanel(type);
};

const getScrollTop = () => {
    let scrollTop = document.documentElement.scrollTop || document.body.scrollTop;
    return scrollTop;
};

const initScroll = () => {
    let initScrollTop = getScrollTop();
    window.addEventListener('scroll', () => {
        let currentScrollTop = getScrollTop();
        if (currentScrollTop > initScrollTop && currentScrollTop > 100) {
            showHeader.value = false;
        } else {
            showHeader.value = true;
        }
        initScrollTop = currentScrollTop;
    });
};

const getUserInfo = async () => {
    let result = await proxy.Request({
        url: '/user/getLoginUserInfo',
        dataType: "json",
        method: 'get',
    });

    if(!result){
        return;
    }
    store.commit('updateLoginUserInfo', result);
};

const boardList = ref([
    {
        boardName: "公告",
        boardId: "announcement",
    },
    {
        boardName: "校园生活",
        boardId: "campus_life",
    },
    {
        boardName: "学习交流",
        boardId: "study_exchange",
    },
    {
        boardName: "校友闲谈",
        boardId: "student_chat",
    },
    {
        boardName: "校友求助",
        boardId: "student_help",
    },
])

const saveBoardId = (boardIdValue) => {
    store.commit('saveBoardId', boardIdValue);
}

const getBoardId = () => {
    return store.state.boardId;
}

const loadBoard = () =>{
    store.commit('saveBoardList', boardList.value);
}
loadBoard();

const currentYear = new Date().getFullYear()

const yearList = ref([
    {
        yearName: `${currentYear - 4}级及以前`,
        yearId: currentYear - 4,
    },
    {
        yearName: `${currentYear - 3}级`,
        yearId: currentYear - 3,
    },
    {
        yearName: `${currentYear - 2}级`,
        yearId: currentYear - 2,
    },
    {
        yearName: `${currentYear - 1}级`,
        yearId: currentYear - 1,
    },
    {
        yearName: `${currentYear}级`,
        yearId: currentYear,
    },
]);

const loadYear = () =>{
    store.commit('saveYearList', yearList.value);
}
loadYear();

const roleList = ref([
    {
        roleName: "学生",
        roleId: 'student',
    },
    {
        roleName: "老师",
        roleId: 'teacher',
    }
]);

const loadRole = () =>{
    store.commit('saveRoleList', roleList.value);
}
loadRole();

const boardClickHandler = (board)=>{
    router.push({ path: '/forum/' + board.boardId });
}

watch(
    () => store.state.loginUserInfo,
    (newVal, oldVal) =>{
        if(newVal != undefined && newVal != null){
            userInfo.value = newVal;
        }else{
            userInfo.value = {};
        }
    },
    { immediate: true, deep: true }
);

watch(
    () => store.state.showLoginDialog,
    (newVal, oldVal) =>{
        if(newVal){
            login(1);
            store.commit('showLoginDialog', false);
        }
    },
    { immediate: true, deep: true }
);

onMounted(() => {
    initScroll();
    getUserInfo();
});

</script>

<template>
    <div>
        <div class="header" v-show="showHeader">
            <div 
            class="header-content" 
            :style="{ width: proxy.globalInfo.bodyWidth + 'px' }"
            >
                <router-link to="/" class="logo">广东校园论坛</router-link>
                <div class="menu-panel">
                    <router-link :class="['menu-item',  getBoardId() == '0' ? 'active' : '']" to="/" >首页</router-link>
                    <template v-for="board in boardList">
                        <span :class="['menu-item', board.boardId == getBoardId() ? 'active' : '']" @click="boardClickHandler(board)">{{ board.boardName }}</span>
                    </template>
                </div>
                <div class="user-info-panel">
                    <el-button type="primary">
                        发帖<span class="iconfont icon-add"></span>
                    </el-button>
                    <el-button type="primary">
                        搜索<span class="iconfont icon-search"></span>
                    </el-button>
                    <div v-if="userInfo.userId">
                        <div class="message-info">
                            <el-dropdown class="message-dropdown">
                                <el-badge 
                                    :value="12"
                                    class="item"
                                    >
                                    <div class="iconfont icon-message">
                                    </div>
                                </el-badge>
                                <template #dropdown>
                                    <el-dropdown-menu>
                                        <el-dropdown-item>回复我的</el-dropdown-item>
                                        <el-dropdown-item>赞了我的帖子</el-dropdown-item>
                                        <el-dropdown-item>赞了我的评论</el-dropdown-item>
                                        <el-dropdown-item>系统消息</el-dropdown-item>
                                    </el-dropdown-menu>
                                </template>
                            </el-dropdown>
                        </div>
                        <div class="user-info">
                            <el-dropdown>
                                <avatar :userId="userInfo.userId"></avatar>
                                <template #dropdown>
                                        <el-dropdown-menu>
                                        <el-dropdown-item>个人主页</el-dropdown-item>
                                        <el-dropdown-item>退出登录</el-dropdown-item>
                                    </el-dropdown-menu>
                                </template>
                            </el-dropdown>
                        </div>
                    </div>
                    <el-button v-else type="primary" plain @click="login(1)">登录</el-button>
                </div>
            </div>
        </div>
        <div class="body-content">
            <router-view />
        </div>
        <Login ref="loginRef"></Login>
    </div>
</template>

<style scoped lang="scss">
.header {
    background-color: rgb(255, 255, 255);
    top: 0;
    width: 100%;
    position: fixed;
    box-shadow: 0 2px 6px 0 #ddd;
    .header-content {
        margin: 0 auto;
        align-items: center;
        height: 60px;
        display: flex;
        align-items: center;
        .logo {
            display: block;
            margin-right: 5px;
            text-decoration: none;
            font-size: 25px;
        }
        .menu-panel {
            flex: 1;
            .menu-item {
                margin-left: 20px;
                cursor: pointer;
                text-decoration: none;
                color : #000000;
            }
            .active{
                color: var(--link);
            }
        }
        .user-info-panel {
            display: flex;
            width: 300px;
            align-items: center;
            .message-info{
                cursor: pointer;
                margin-left: 10px;
                .icon-message{
                    font-size: 25px;
                    color :rgb(147, 147, 147);
                }
            }
            .user-info{
                margin-left: 20px;
            }
        }
    }
}
.body-content {
    margin-top: 60px;
    position: relative;
}
</style>