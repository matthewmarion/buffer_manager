# Buffer Manager
This is a buffer manager implementation in C++ for managing pages of a database. It is used to reduce the number of I/O operations that are necessary to access frequently used data.

## Getting Started
To get started, clone the repository to your local machine and include the `buffer_manager.h` header in your project. The code is written in C++ and requires a C++ compiler to build.

## How to Use
The BufferManager class provides the main interface for managing the buffer frames. To use the buffer manager, create an instance of the `BufferManager` class with the desired page size and page count:

```cpp
BufferManager bufferManager(page_size, page_count);
```
  
To access a page, call the fix_page() method with the page ID and a boolean value indicating whether exclusive access is required:
  
```cpp
BufferFrame& bufferFrame = bufferManager.fix_page(page_id, exclusive);
```
The fix_page() method returns a reference to a BufferFrame object that represents the requested page. The exclusive parameter indicates whether exclusive access to the page is required. If exclusive access is required, other threads will not be able to access the page until it is released.

To release a page, call the unfix_page() method with a reference to the BufferFrame object:

```cpp
bufferManager.unfix_page(bufferFrame);
```
  
The BufferFrame class provides methods for accessing and modifying the page data:

```cpp
char* data = bufferFrame.get_data(); // get a pointer to the page data
bufferFrame.lockPage(true); // lock the page for exclusive access
bufferFrame.unlockPage(true); // unlock the page and indicate that the page has been modified
```
## Contributing
If you find a bug or have a feature request, please open an issue. Pull requests are also welcome.
