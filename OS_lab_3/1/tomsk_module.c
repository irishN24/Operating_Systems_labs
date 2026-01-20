#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nazarova Irina");
MODULE_DESCRIPTION("A simple Tomsk State University kernel module");
MODULE_VERSION("1.0");

// Функция загрузки модуля
static int __init tosk_module_init(void)
{
    printk(KERN_INFO "Welcome to the Tomsk State University\n");
    return 0;
}
// Функция выгрузки модуля
static void __exit tosk_module_exit(void)
{
    printk(KERN_INFO "Tomsk State University forever!\n");
}
// Функции для загрузки и выгрузки
module_init(tosk_module_init);
module_exit(tosk_module_exit);
