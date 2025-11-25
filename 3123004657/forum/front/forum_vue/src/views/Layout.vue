<script setup>
import { ref, reactive, getCurrentInstance, onMounted } from 'vue';
import { useRouter, useRoute } from 'vue-router';
import Login from './Login.vue';
const {proxy} = getCurrentInstance();
const router = useRouter();
const route = useRoute();
const showHeader = ref(true);
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
const loginRef = ref();
const login = (type) => {
    loginRef.value.showPanel(type);
};
onMounted(() => {
    initScroll();
});
</script>

<template>
    <div>
        <div class="header" v-show="showHeader">
            <div class="header-content" :style="{ width: proxy.globalInfo.bodyWidth + 'px' }">
                <router-link to="/" class="logo">广东校园论坛</router-link>
                <div class="menu-panel"></div>
                <div class="user-info-panel">
                    <el-button type="primary">
                        发帖<span class="iconfont icon-add"></span>
                    </el-button>
                    <el-button type="primary">
                        搜索<span class="iconfont icon-search"></span>
                    </el-button>
                        <el-button type="primary" plain @click="login(1)">登录</el-button>
                </div>
            </div>
        </div>
        <div>
            <router-view></router-view>
        </div>
        <Login ref="loginRef"></Login>
    </div>
</template>

<style scoped lang="scss">
.header {
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
        }
        .user-info-panel {
            display: flex;
            width: 300px;
        }
    }
}
</style>