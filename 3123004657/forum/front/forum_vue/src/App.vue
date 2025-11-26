<script setup>
import { reactive } from 'vue';
import znCn from 'element-plus/es/locale/lang/zh-cn';
const locale = znCn;
const config = reactive({
    max: 1
});
</script>

<template>
  <el-config-provider :locale="locale" :message="config">
    <RouterView />
  </el-config-provider>
</template>

<style scoped>

</style>
