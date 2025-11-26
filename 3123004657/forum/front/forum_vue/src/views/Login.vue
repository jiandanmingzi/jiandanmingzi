<script setup>
import { dataType } from 'element-plus/es/components/table-v2/src/common';
import { ref, reactive, getCurrentInstance, onMounted, nextTick } from 'vue';
import { useRouter, useRoute } from 'vue-router';
const {proxy} = getCurrentInstance();
const router = useRouter();
const route = useRoute();
const opType = ref();

//初始化表单数据
const formData = ref({});
const formDataRef = ref();

//表单弹窗配置
const dialogConfig = reactive({
    show:false,
    title:"标题",
});

//设置表单标题
const setFormTitle = (type) => {
    if (type == 1) {
        dialogConfig.title = "用户登录";
    } else {
        dialogConfig.title = "重置密码";
    }
}
//重置表单
const resetForm = () =>{
    if (!formDataRef.value) {
        return;
    }
    nextTick(() => {
        formDataRef.value.resetFields();
    });
}

//显示表单面板,type:1-登录,0-重置密码
const showPanel = (type) => {
    resetForm();
    opType.value = type;
    setFormTitle(type);
    dialogConfig.show = true;
};
defineExpose({
    showPanel
});

//校验确认密码
const checkRePassword = (rule, value, callback) => {
    if (value !== formData.value.newPassword) {
        callback(new Error(rule.message));
    } else {
        callback();
    }
};

//表单校验规则
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

//接口地址
const api = {
    login:"/api/auth/login",
    changePassword:"/api/users/id/password"
}

//提交表单
const doSubmit = () => {
    formDataRef.value.validate(async(valid) => {
        if (!valid) {
            return;
        }
        let params ={};
        Object.assign(params, formData.value);
        
        //设置url
        let url = "";
        if (opType.value == 1) {
            url = api.login;
        } else {
            url = api.changePassword;
        }

        let result = await proxy.Request({
            url:url,
            params:params,
            dataType: 'json',
        })
        if (!result) {
            return;
        }

        if (opType.value == 1) {
            proxy.Message.success("登录成功");
            dialogConfig.show = false;
            router.go(0);
        } else {
            proxy.Message.success("密码重置成功，请使用新密码登录");
            showPanel(1);
        }
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
        <el-form-item>
            <el-button 
            type="primary" 
            size="large" 
            style="width: 100%;"
            @click="doSubmit"
            >
                <span v-if="opType == 1">登 录</span>
                <span v-else>确 定</span>
            </el-button>
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