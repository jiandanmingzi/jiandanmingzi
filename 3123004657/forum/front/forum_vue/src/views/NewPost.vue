<script setup>
import { ref, reactive, getCurrentInstance, watch } from 'vue';
import { useRouter, useRoute } from 'vue-router';
import { useStore } from 'vuex';
import { ElMessage } from 'element-plus';

const { proxy } = getCurrentInstance();
const router = useRouter();
const route = useRoute();
const store = useStore();
const isAnonymous = ref(false);

const api = {
    handleCreatePost: "/posts",
    handleUpdatePost: "/posts/id",
    handleGetPostDetail: '/posts/handleGetPostDetail',
}

const formData = ref({
    title: '',
    boardId: '',
    content: '',
    markdownContent: '', // 如果后端需要纯markdown文本
});

const formRef = ref();
const boardList = ref([]);

const loadBoardList = () => {
    if (store.getters.getLoginUserInfo && store.getters.getLoginUserInfo.data.role == "student") {
        boardList.value = store.getters.getBoardList.filter(item => item.boardId != "announcement");
    } else {
        boardList.value = store.getters.getBoardList;
    }
};

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

        console.log("提交的文章参数：", params);

        let result;
        if (articleId.value) {
            params.post_id = articleId.value;
            result = await proxy.Request({
                url: api.handleUpdatePost,
                params: params,
                method: 'put',
            });
        } else {
            result = await proxy.Request({
                url: api.handleCreatePost,
                params: params,
                method: 'post',
            });
        }

        if (!result) {
            return;
        }

        ElMessage.success(articleId.value ? '修改成功' : '发布成功');
        router.push(`/`);
    });
};

// 监听编辑器变化
const handleEditorChange = (text, html) => {
    formData.value.markdownContent = text;
    formData.value.content = html;
};

const articleId = ref(null);
const getArticleDetail = async () => {
    let result = await proxy.Request({
        url: api.handleGetPostDetail,
        method: 'POST',
        dataType: "json",
        params: {
            post_id: articleId.value,
        },
    });

    if (result && result.data) {
        let data = result.data;
        formData.value.title = data.title;
        formData.value.category = data.category;
        formData.value.markdownContent = data.content;
        formData.value.content = data.content; // 假设编辑器能处理
        isAnonymous.value = data.is_anonymous == 1;
    }
};

watch(
    () => route.params,
    (newVal) => {
        if (newVal.articleId) {
            articleId.value = parseInt(newVal.articleId, 10);
            getArticleDetail();
        } else {
            articleId.value = null;
            formData.value = {
                title: '',
                boardId: '',
                content: '',
                markdownContent: '',
            };
        }
    },
    { immediate: true }
);

watch(
    () => [store.state.loginUserInfo, store.state.boardList],
    () => {
        loadBoardList();
    },
    { immediate: true }
);

</script>

<template>
    <div class="body-container" :style="{ width: proxy.globalInfo.bodyWidth + 'px' }">
        <div class="post-panel">
            <div class="panel-title">
                发布文章
                <div class="anonymous-checkbox">
                    <el-checkbox v-model="isAnonymous" :disabled="articleId != null">匿名发布</el-checkbox>
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
                    <el-select v-model="formData.category" placeholder="请选择板块" style="width: 100%;"
                        :disabled="articleId != null">
                        <el-option v-for="item in boardList" :key="item.boardId" :label="item.boardName"
                            :value="item.boardId"></el-option>
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