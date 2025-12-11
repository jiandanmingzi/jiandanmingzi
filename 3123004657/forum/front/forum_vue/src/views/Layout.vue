<script setup>
import { ref, reactive, getCurrentInstance, onMounted, watch } from 'vue';
import { useRouter, useRoute } from 'vue-router';
import Login from './Login.vue';
import { useStore } from 'vuex';
const store = useStore();
const { proxy } = getCurrentInstance();
const router = useRouter();
const route = useRoute();
const userInfo = ref({});
const showHeader = ref(true);
const loginRef = ref();
const unReadCount = ref(0);

const api = {
    handleGetUnreadCount: "/notifications/unread-count",
    handleLogout: "/auth/logout",
}

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

const getUnReadNoticeCount = async () => {
    let result = await proxy.Request({
        url: api.handleGetUnreadCount,
        dataType: "json",
        method: 'get',
    });

    if (!result) {
        return 0;
    }
    unReadCount.value = result.data.unread_count;
};

const getUserInfo = async () => {
    let result = await proxy.Request({
        url: '/users/id',
        dataType: "json",
        method: 'get',
    });

    if (!result) {
        return;
    }
    store.commit('updateLoginUserInfo', result);
    console.log("Fetched user info:", result);
    getUnReadNoticeCount();
};

getUserInfo();

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

const loadBoard = () => {
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

const loadYear = () => {
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

const loadRole = () => {
    store.commit('saveRoleList', roleList.value);
}
loadRole();

const boardClickHandler = (board) => {
    router.push({ path: '/forum/' + board.boardId });
}

watch(
    () => store.state.loginUserInfo,
    (newVal, oldVal) => {
        if (newVal != undefined && newVal != null) {
            userInfo.value = newVal.data;
            console.log("userInfo updated:", userInfo.value);
        } else {
            userInfo.value = {};
        }
    },
    { immediate: true, deep: true }
);

watch(
    () => store.state.hasLogin,
    (newVal, oldVal) => {
        if (!newVal) {
            userInfo.value = {};
        } else {
            getUserInfo();
        }
    },
    { immediate: true, deep: true }
)

watch(
    () => store.state.showLoginDialog,
    (newVal, oldVal) => {
        if (newVal) {
            login(1);
            store.commit('showLoginDialog', false);
        }
    },
    { immediate: true, deep: true }
);

const handlePostPost = () => {
    if (store.state.loginUserInfo == null) {
        store.commit('showLoginDialog', true);
        proxy.$message.warning("请先登录！");
        return;
    }
    router.push({ path: '/newPost' });
}

const goToPersonalCenter = () => {
    if (store.state.loginUserInfo == null) {
        store.commit('showLoginDialog', true);
        proxy.$message.warning("请先登录！");
        return;
    }
    router.push({ path: '/personalCenter' });
}

const goToNoticeCenter = () => {
    if (store.state.loginUserInfo == null) {
        store.commit('showLoginDialog', true);
        proxy.$message.warning("请先登录！");
        return;
    }
    router.push({ path: '/notice' });
}

const handleSearch = () => {
    router.push({ path: '/search' });
}

onMounted(() => {
    initScroll();
});

const logout = async () => {
    await proxy.Request({
        url: api.handleLogout,
        dataType: "json",
        method: 'post',
    });
    store.commit('updateHasLogin', false);
    store.commit('updateLoginUserInfo', {});
    router.push({ path: '/' });
    proxy.$message.success("已退出登录");
};
</script>

<template>
    <div>
        <div class="header" v-show="showHeader">
            <div class="header-content" :style="{ width: proxy.globalInfo.bodyWidth + 'px' }">
                <router-link to="/" class="logo">广工校园论坛</router-link>
                <div class="menu-panel">
                    <router-link :class="['menu-item', getBoardId() == '0' ? 'active' : '']" to="/">首页</router-link>
                    <template v-for="board in boardList">
                        <span :class="['menu-item', board.boardId == getBoardId() ? 'active' : '']"
                            @click="boardClickHandler(board)">{{ board.boardName }}</span>
                    </template>
                </div>
                <div class="user-info-panel">
                    <el-button type="primary" @click="handlePostPost()">
                        发帖<span class="iconfont icon-add"></span>
                    </el-button>
                    <el-button type="primary" @click="handleSearch()">
                        搜索<span class="iconfont icon-search"></span>
                    </el-button>
                    <div v-if="userInfo.account">
                        <div class="message-info">
                            <el-dropdown class="message-dropdown" @click="goToNoticeCenter()">
                                <el-badge :value="unReadCount" :hidden="unReadCount <= 0" class="item">
                                    <div class="iconfont icon-message">
                                    </div>
                                </el-badge>
                                <template #dropdown>
                                    <el-dropdown-menu>
                                        <el-dropdown-item @click="goToNoticeCenter()">回复我的</el-dropdown-item>
                                    </el-dropdown-menu>
                                </template>
                            </el-dropdown>
                        </div>
                        <div class="user-info">
                            <el-dropdown>
                                <avatar :userId="userInfo.account"></avatar>
                                <template #dropdown>
                                    <el-dropdown-menu>
                                        <el-dropdown-item @click="goToPersonalCenter()">个人主页</el-dropdown-item>
                                        <el-dropdown-item @click="logout()">退出登录</el-dropdown-item>
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
                color: #000000;
            }

            .active {
                color: var(--link);
            }
        }

        .user-info-panel {
            display: flex;
            align-items: center;
            flex-wrap: wrap;
        }

        .user-info-panel>*:not(:last-child) {
            margin-right: 10px;
        }

        .user-info-panel>div {
            display: flex;
            align-items: center;
        }

        .user-info-panel>div>*:not(:last-child) {
            margin-right: 15px;
        }

        .message-info {
            cursor: pointer;

            .icon-message {
                font-size: 25px;
                color: rgb(147, 147, 147);
            }
        }
    }
}

.body-content {
    margin-top: 60px;
    position: relative;
}
</style>