<script setup>
import Loading from 'element-plus/es/components/loading/src/service';

const props = defineProps({
    dataSource:{
        type:Object
    },
    loading:{
        type: Boolean,
    }
})

const emit =  defineEmits(["loadData"]);
const handlePageNoChange = (pageNo) => {
    props.dataSource.pageNo = pageNo;
    emit("loadData")
};
</script>

<template>
<div class="" v-if="!loading && (dataSource == null || dataSource.dataList == null || dataSource.dataList.length == 0)">
    <NoData :msg="'空空如也'"></NoData>
</div>
<div class="skeleton" v-if="loading">
    <el-skeleton row="2" animated></el-skeleton>
</div>
<div v-for="item in dataSource.dataList" v-if="!loading">
    <slot :data="item"></slot>
</div>
<div class="pagination">
    <el-pagination
        v-if="dataSource.pageTotal > 1"
        background
        :total="dataSource.totalCount"
        :current-page.sync="dataSource.pageNo"
        layout="prev, pager, next"
        @current-change="handlePageNoChange"
        style="text-align: right"
        >
    </el-pagination>
</div>
</template>

<style scoped lang="scss">
.skeleton{
    padding: 15px;
}
.pagination{
    padding: 10px 0px 10px 10px;
}
</style>