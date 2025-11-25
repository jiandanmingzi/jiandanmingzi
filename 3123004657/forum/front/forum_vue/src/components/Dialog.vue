<script setup>
const props = defineProps({
    show: {
        type: Boolean,
        default: true
    },
    
    showCancel: {
        type: Boolean,
        default: true
    },
    showClose: {
        type: Boolean,
        default: true
    },
    title: {
        type: String,
        default: "标题"
    },
    width: {
        type: String,
        default: "30%"
    },
    top: {
        type: String,
        default: "50px"
    },
    buttons: {
        type: Array,
        default: () => []
    }
});
const emit = defineEmits();
const close = () =>{
    emit("close");
};
</script>

<template>
    <div>
        <el-dialog 
            class="custom-dialog"
            :model-value="show" 
            :close-on-click-modal="false"
            :draggable="true"
            :title="title" 
            :show-close="showClose" 
            :top="top"
            :width="width"
            @close="close"
        >
            <div class="dialog-body">
                <slot></slot>
            </div>
            <template v-if="buttons && buttons.length > 0 || showCancel">
                <div class="dialog-footer">
                    <el-button 
                        link @click="close" 
                        v-if="showCancel"
                    >
                        取消
                    </el-button>
                    <el-button
                        v-for="btn in buttons"
                        :type="btn.type" 
                        @click="btn.click"
                        >
                        {{ btn.text }}
                    </el-button>
                </div>
            </template>
        </el-dialog>
    </div>
</template>

<style scoped lang="scss">
.custom-dialog {
    .dialog-body {
        border-top: 1px solid #ddd;
        border-bottom: 1px solid #ddd;
        min-height: 80px;
        max-height: calc(100vh - 220px);
        overflow: auto;
    }
    .dialog-footer {
        text-align: right;
        padding: 5px 20px;
    }
}
</style>