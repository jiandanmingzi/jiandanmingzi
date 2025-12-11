<script setup>
import { ref, reactive, getCurrentInstance, watch } from 'vue';
import { useRouter } from 'vue-router';
import { useStore } from 'vuex';
import { ElMessage } from 'element-plus';

const { proxy } = getCurrentInstance();
const router = useRouter();
const store = useStore();
const isAnonymous = ref(false);

const api = {
    handleCreatePost: "/posts",
    handleUpdatePost: "/posts/id",
}

const formData = ref({
    title: '',
    boardId: '',
    content: '',
    markdownContent: '', // 如果后端需要纯markdown文本
});

const formRef = ref();
const boardList = ref([]);

// 获取板块列表，用于发帖选择
const loadBoardList = () => {
    boardList.value = store.state.boardList || [];
};
// 监听store变化，防止刷新页面时store未初始化
watch(
    () => store.state.boardList,
    (newVal) => {
        boardList.value = newVal;
    },
    { immediate: true }
);

const rules = {
    title: [{ required: true, message: '请输入标题', trigger: 'blur' }],
    category: [{ required: true, message: '请选择板块', trigger: 'change' }],
    content: [{ required: true, message: '请输入正文内容', trigger: 'blur' }],
};

// 编辑器配置：移除图片上传相关功能
const toolbarConfig = 'h bold italic strikethrough quote | ul ol table hr | link code | undo redo clear | save';

const postHandler = () => {
    formRef.value.validate(async (valid) => {
        if (!valid) {
            return;
        }

        let params = {
            title: formData.value.title,
            category: formData.value.category,
            content: formData.value.markdownContent, // Markdown源码
            is_anonymous: isAnonymous.value
        };

        console.log("提交参数：", params);

        // API 接口调用预留
        let result = await proxy.Request({
            url: api.handleCreatePost,
            params: params,
            method: 'post', // 或根据后端要求
        });

        if (!result) {
            return;
        }

        ElMessage.success('发布成功');
        router.push(`/post/${result.data.post_id}`);
    });
};

// 监听编辑器变化
const handleEditorChange = (text, html) => {
    formData.value.markdownContent = text;
    formData.value.content = html;
};

</script>

<template>
    <div class="body-container" :style="{ width: proxy.globalInfo.bodyWidth + 'px' }">
        <div class="post-panel">
            <div class="panel-title">
                发布文章
                <div class="anonymous-checkbox">
                    <el-checkbox v-model="isAnonymous">匿名发布</el-checkbox>
                </div>
            </div>
            <el-form :model="formData" :rules="rules" ref="formRef" label-width="0px">
                <!-- 标题输入 -->
                <el-form-item prop="title">
                    <el-input v-model="formData.title" placeholder="请输入标题" maxlength="50" show-word-limit
                        class="title-input"></el-input>
                </el-form-item>

                <!-- 板块选择 -->
                <el-form-item prop="boardId">
                    <el-select v-model="formData.category" placeholder="请选择板块" style="width: 100%;">
                        <el-option v-for="item in boardList" :key="item.category" :label="item.boardName"
                            :value="item.category"></el-option>
                    </el-select>
                </el-form-item>

                <!-- Markdown 编辑器 -->
                <el-form-item prop="content">
                    <v-md-editor v-model="formData.markdownContent" :height="'500px'" :disabled-menus="[]"
                        :left-toolbar="toolbarConfig" placeholder="请输入正文内容（不支持图片上传）"
                        @change="handleEditorChange"></v-md-editor>
                </el-form-item>

                <!-- 提交按钮 -->
                <el-form-item>
                    <el-button type="primary" @click="postHandler" class="post-btn">发布文章</el-button>
                </el-form-item>
            </el-form>
        </div>
    </div>
</template>

<style scoped lang="scss">
.body-container {
    margin-top: 10px;

    .post-panel {
        background: #fff;
        padding: 20px;
        border-radius: 5px;

        .panel-title {
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 20px;
            font-weight: bold;
            margin-bottom: 20px;
            color: #333;
        }

        .title-input {
            font-size: 16px;
            font-weight: bold;

            :deep(.el-input__wrapper) {
                box-shadow: none;
                border-bottom: 1px solid #ddd;
                border-radius: 0;
                padding-left: 0;
            }
        }

        .post-btn {
            width: 150px;
        }
    }
}
</style>