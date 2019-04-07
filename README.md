[Lissandra](https://faq.utnso.com/lissandra) - Base de datos distribuida de pares Clave-Valor
============================================================================
## Entorno
#####Alternativas:
1. [Visual Studio (Windows)](https://github.com/sisoputnfrba/tp-2019-1c-foo-bar/wiki/Entorno:-Visual-Studio) (recomendado)
2. [Eclipse Onyx (Lubuntu)](https://github.com/sisoputnfrba/tp-2019-1c-foo-bar/wiki/Entorno:-Eclipse-Onyx)

####Comparación
| Ide           | Productividad    | Setup   | Beginner-friendly | Open Source | Puntaje |
|:--------------|:----------------:|:-------:|:-----------------:|:-----------:|:-----------:|
| Visual Studio |Alta              |[Difícil?](https://github.com/sisoputnfrba/tp-2019-1c-foo-bar/wiki/Entorno:-Visual-Studio) | ✓                 | ✖          |  9    |
| Eclipse       |Baja              |[Fácil](https://github.com/sisoputnfrba/tp-2019-1c-foo-bar/wiki/Entorno:-Eclipse-Onyx)    | Lo dudo           | ✓           | 1 :(  |

-------------------------------------------------------------
## Estructura
| Directorio | Descripción  |
| ----------|:-------------|
| cx        |proyecto cx (biblioteca con utilidades de propósito general "c-xtendido")|
| ker       |proyecto nodo Kernel (balanceador de carga)|
| mem       |proyecto nodo Memory (memoria caché)|
| lfs       |proyecto nodo Lissandra Fs (persistencia)|
| common    |archivos específicos del sistema a compartir entre nodos|
| scripts   |scripts y utilidades varias|

-------------------------------------------------------------
## Compilación
Sistema de compilación basado en la herramienta make ([introducción](https://www.youtube.com/watch?v=OHfMNqe-Fdw), [cheatsheet](https://devhints.io/makefile)) con 2 targets DEBUG y RELEASE. Diferencias:

| Target    | Assertions    | Info | Warnings | CX lib  |
| ----------|:-------------:|:-----:|:-------:|:-------:|
| DEBUG     |Sí             |Sí     |Sí       |libcxd.so|
| RELEASE   |No             |No     |No       |libcx.so |

####Comandos make soportados:
* **make clean** - limpia el directorio build que contiene archivos de builds previas
* **make debug** - compila una build debug
* **make release** / make / make all - compila una build release 
* **make valgrind** - corre valgrind sobre la build debug para diagnosticar memory leaks

####Salida:
#####cx (shared library):
```
cx/build/debug/libcx.so (debug)
cx/build/release/libcxd.so (release)
```
#####nodos:
```
[PROJECT_NAME]/build/[TARGET_NAME]/[PROJECT_NAME].out
```
* [PROJECT_NAME]: ker, mem, lfs o cx
* [TARGET_NAME]: debug o release
####Ejecución:
Ejemplo para correr nodo Kernel (debug build):
```
$ cd /home/utnso/lissandra/ker
$ ./build/debug/ker.out
```

-------------------------------------------------------------
## [Programación Defensiva](https://github.com/sisoputnfrba/tp-2019-1c-foo-bar/wiki/Programaci%C3%B3n-Defensiva)
-------------------------------------------------------------
## [Estilo de Código y Convenciones de Nombres](https://github.com/sisoputnfrba/tp-2019-1c-foo-bar/wiki/Estilo-de-C%C3%B3digo-y-Convenciones-de-Nombres)
-------------------------------------------------------------

