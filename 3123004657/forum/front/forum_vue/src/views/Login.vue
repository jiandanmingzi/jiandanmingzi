<script setup>
import { ref, reactive, getCurrentInstance, onMounted, nextTick } from 'vue';
import { useRouter, useRoute } from 'vue-router';
const {proxy} = getCurrentInstance();
const router = useRouter();
const route = useRoute();
const opType = ref();
const showPanel = (type) => {
    opType.value = type;
    resetForm();
    
};
defineExpose({
    showPanel
});
const api = {
    
}
const formData = ref({});
const formDataRef = ref();
const checkRePassword = (rule, value, callback) => {
    if (value !== formData.value.newPassword) {
        callback(new Error(rule.message));
    } else {
        callback();
    }
};
const rules = {
    account:[
        {required: true, message: '请输入学号', trigger: 'blur'},
        {max: 10, message: '学号不超过10位', trigger: 'blur'},
        {validator: proxy.Verify.number, message: '学号仅包含数字', trigger: 'blur'}
    ],
    password:[
        {required: true, message: '请输入密码', trigger: 'blur'}
    ],
    orgPassword:[
        {required: true, message: '请输入原密码', trigger: 'blur'}
    ],
    newPassword:[
        {required: true, message: '请输入新密码', trigger: 'blur'},
        {max: 20, message: '密码不超过20位', trigger: 'blur'},
        {min: 6, message: '密码不少于6位', trigger: 'blur'}
    ],
    reNewPassword:[
        {required: true, message: '请再次输入新密码', trigger: 'blur'},
        {validator: checkRePassword, message: '两次输入密码不一致', trigger: 'blur'}
    ],
};
const dialogConfig = reactive({
    show:false,
    title:"标题",
});

const resetForm = () =>{
    dialogConfig.show = true;
    if (opType.value == 1) {
        dialogConfig.title = "用户登录";
    } else {
        dialogConfig.title = "重置密码";
    }
    nextTick(() => {
        formDataRef.value.resetFields();
    });
}
</script>

<template>
<div>
    <Dialog
        :show="dialogConfig.show"
        :title="dialogConfig.title"
        :buttons="dialogConfig.buttons"
        width="400px"
        @close="dialogConfig.show = false"
        >
    <el-form 
        class="login"
        ref="formDataRef"
        :model="formData"
        :rules="rules"
        >
        <el-form-item prop="account">
            <el-input
                size="large"
                clearable
                placeholder="请输入学号"
                v-model="formData.account"
            >
                <template #prefix>
                    <span class="iconfont icon-account"></span>
                </template>
            </el-input>
        </el-form-item>
        <!--
        <el-form-item prop="nickName" v-if="opType == 0">
            <el-input
                size="large"
                clearable
                placeholder="请输入昵称"
                v-model="formData.nickName"
            >
                <template #prefix>
                    <span class="iconfont icon-account"></span>
                </template>
            </el-input>
        </el-form-item>
        <el-form-item prop="registerPassword" v-if="opType == 0">
            <el-input
                size="large"
                clearable
                placeholder="请输入密码"
                v-model="formData.registerPassword"
            >
                <template #prefix>
                    <span class="iconfont icon-password"></span>
                </template>
            </el-input>
        </el-form-item>
        <el-form-item prop="rePassword" v-if="opType == 0">
            <el-input
                size="large"
                clearable
                placeholder="请再次输入密码"
                v-model="formData.rePassword"
            >
                <template #prefix>
                    <span class="iconfont icon-password"></span>
                </template>
            </el-input>
        </el-form-item>
        -->
        <el-form-item prop="password" v-if="opType == 1">
            <el-input
                show-password
                size="large"
                clearable
                placeholder="请输入密码"
                v-model="formData.password"
            >
                <template #prefix>
                    <span class="iconfont icon-password"></span>
                </template>
            </el-input>
        </el-form-item>
        <el-form-item prop="orgPassword" v-if="opType == 0">
            <el-input
                show-password
                size="large"
                clearable
                placeholder="输入原密码"
                v-model="formData.orgPassword"
            >
                <template #prefix>
                    <span class="iconfont icon-password"></span>
                </template>
            </el-input>
        </el-form-item>
        <el-form-item prop="newPassword" v-if="opType == 0">
            <el-input
                show-password
                size="large"
                clearable
                placeholder="输入新密码"
                v-model="formData.newPassword"
            >
                <template #prefix>
                    <span class="iconfont icon-password"></span>
                </template>
            </el-input>
        </el-form-item>
        <el-form-item prop="reNewPassword" v-if="opType == 0">
            <el-input
                show-password
                size="large"
                clearable
                placeholder="再次输入新密码"
                v-model="formData.reNewPassword"
            >
                <template #prefix>
                    <span class="iconfont icon-password"></span>
                </template>
            </el-input>
        </el-form-item>
        <el-form-item>
            <div class="rememberme-panel" v-if="opType == 1">
                <el-checkbox v-model="formData.rememberMe">记住我</el-checkbox>
            </div>
            <div 
                class="change-password-panel" 
                v-if="opType == 1"
                @click="showPanel(0)"
                >
                <a href="javascript:void(0)" class="a-link">重置密码</a>
            </div>
            <div 
                class="go-login-panel" 
                v-if="opType == 0"
                @click="showPanel(1)"
                >
                <a href="javascript:void(0)" class="a-link">去登陆</a>
            </div>
        </el-form-item>    
        <el-form-item v-if="opType == 1">
            <el-button type="primary" size="large" style="width: 100%;">登录</el-button>
        </el-form-item>
        <el-form-item v-if="opType == 0">
            <el-button type="primary" size="large" style="width: 100%;">确认重置</el-button>
        </el-form-item>
    </el-form>
    </Dialog>
</div>
</template>

<style scoped lang="scss">
.login {
    .rememberme-panel {
        margin-right: auto
    }
    .change-password-panel {
        margin-left: auto;
        .a-link {
            text-decoration: none;
            color: #409eff
        }
    }
    .go-login-panel {
        margin-right: auto;
        .a-link {
            text-decoration: none;
            color: #409eff
        }
    }
}
</style>