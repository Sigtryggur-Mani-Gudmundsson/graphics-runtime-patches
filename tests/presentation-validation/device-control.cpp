#include <vulkan/vulkan.h>
#include <cstdio>
#include <vector>

int main() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Vulkan device control";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instance_info, nullptr, &instance) != VK_SUCCESS) return 1;

    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS || !count) return 2;
    std::vector<VkPhysicalDevice> devices(count);
    if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) != VK_SUCCESS) return 3;
    VkPhysicalDevice gpu = devices.front();
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(gpu, &props);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    if (family == count) return 4;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    VkDevice device = VK_NULL_HANDLE;
    VkResult result = vkCreateDevice(gpu, &info, nullptr, &device);
    std::printf("%s, requested %u device extensions, vkCreateDevice returned %d\n",
                props.deviceName, info.enabledExtensionCount, int(result));
    if (result == VK_SUCCESS) vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return result == VK_SUCCESS ? 0 : 5;
}
