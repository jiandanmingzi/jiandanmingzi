<script setup>
const props = defineProps({
    msg:{
        type:String,
        default: '暂无数据'
    }
})
</script>

<template>
<div class="no-data">
    <div class="iconfont icon-empty">
        <div class="msg">{{ msg }}</div>
    </div>
</div>
</template>

<style scoped lang="scss">
.no-data{
    text-align: center;
    padding: 10px 0px;
    .icon-empty{
        font-size: 50px;
        color: #bbb;
        .msg{
            font-size: 14px;
            margin-top: 10px;
            color: #909399
        }
    }
}
</style>