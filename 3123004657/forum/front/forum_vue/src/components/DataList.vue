<script setup>
import Loading from 'element-plus/es/components/loading/src/service';

const props = defineProps({
    dataSource: {
        type: Object
    },
    loading: {
        type: Boolean,
    }
})

const emit = defineEmits(["loadData"]);
const handlePageNoChange = (page) => {
    if (props.dataSource.value) {
        props.dataSource.value.page = page;
    } else {
        props.dataSource.page = page;
    }
    emit("loadData")
};
</script>

<template>
    <div class="" v-if="!loading && (dataSource == null || dataSource.data == null || dataSource.data.length == 0)">
        <NoData :msg="'空空如也'"></NoData>
    </div>
    <div class="skeleton" v-if="loading">
        <el-skeleton row="2" animated></el-skeleton>
    </div>
    <div v-for="item in dataSource.data" v-if="!loading" class="dataList">
        <slot :data="item" class="eachData"></slot>
    </div>
    <div class="pagination">
        <el-pagination v-if="dataSource.total_count > dataSource.page_size" background :total="dataSource.total_count"
            :page-size="dataSource.page_size" :current-page.sync="dataSource.page" layout="prev, pager, next"
            @current-change="handlePageNoChange" style="text-align: right">
        </el-pagination>
    </div>
</template>

<style scoped lang="scss">
.skeleton {
    padding: 15px;
}

.dataList {
    margin-bottom: 15px;
}

.pagination {
    padding: 10px 0px 10px 10px;
}
</style>