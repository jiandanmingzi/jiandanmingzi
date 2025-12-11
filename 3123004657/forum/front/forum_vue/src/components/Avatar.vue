<script setup>
import { getCurrentInstance } from 'vue';
import { useRouter, useRoute } from 'vue-router';
const { proxy } = getCurrentInstance();
const router = useRouter();
const route = useRoute();
const props = defineProps({
    userId: {
        type: String,
    },
    width: {
        type: Number,
        default: 50,
    },
    addLink: {
        type: Boolean,
        default: true,
    },
    color: {
        type: String,
        default: '#c0c4cc',
    }
});

const goToUcenter = () => {
    if (props.addLink) {
        if (!props.userId) {
            return;
        }
        router.push({ path: "/ucenter/" + proxy.userId });
    }
};
</script>

<template>
    <div class="avatar" :style="{
        width: width + 'px',
        height: width + 'px',
        'border-radius': (width / 2) + 'px',
        'background-color': color,
        'coursor': addLink ? 'pointer' : 'default'
    }" @click="goToUcenter">
    </div>
</template>

<style scoped lang="scss">
.avatar {
    cursor: pointer;
}
</style>