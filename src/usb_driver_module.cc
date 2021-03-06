#include "usb_driver.h"

#include <v8.h>
#include <node.h>

// Throws a JS error and returns from the current function
#define THROW_AND_RETURN(isolate, msg)                                   \
    do {                                                                 \
        isolate->ThrowException(v8::Exception::TypeError(                \
                    v8::String::NewFromUtf8(isolate, msg)));             \
        return;                                                          \
    }                                                                    \
    while(0)

namespace node_bindings {
    using v8::FunctionCallbackInfo;
    using v8::Isolate;
    using v8::Local;
    using v8::Handle;
    using v8::Persistent;
    using v8::Exception;

    using v8::String;
    using v8::Boolean;
    using v8::Object;
    using v8::Null;
    using v8::Array;
    using v8::Value;


    static Local<Object>
    USBDrive_to_Object(Isolate *isolate, struct usb_driver::USBDrive *usb_drive)
    {
        Local<Object> obj = Object::New(isolate);

#define OBJ_ATTR(name, val)                                                  \
        do {                                                                 \
            Local<String> _name = String::NewFromUtf8(isolate, name);        \
            if (val.size() > 0) {                                            \
                obj->Set(_name, String::NewFromUtf8(isolate, val.c_str()));  \
            }                                                                \
            else {                                                           \
                obj->Set(_name, Null(isolate));                              \
            }                                                                \
        }                                                                    \
        while (0)

        OBJ_ATTR("id", usb_drive->uid);
        OBJ_ATTR("productCode", usb_drive->product_id);
        OBJ_ATTR("vendorCode", usb_drive->vendor_id);
        OBJ_ATTR("product", usb_drive->product_str);
        OBJ_ATTR("serialNumber", usb_drive->serial_str);
        OBJ_ATTR("manufacturer", usb_drive->vendor_str);
        OBJ_ATTR("mount", usb_drive->mount);

#undef OBJ_ATTR
        return obj;
    }

    void
    Unmount(const FunctionCallbackInfo<Value> &info)
    {
        Isolate *isolate = info.GetIsolate();

        if(info.Length() < 1)
            THROW_AND_RETURN(isolate, "Wrong number of arguments");

        if(!info[0]->IsString())
            THROW_AND_RETURN(isolate, "Expected the first argument to by of type string");

        String::Utf8Value utf8_string(info[0]->ToString());
        Local<Boolean> ret;

        if(usb_driver::Unmount(*utf8_string)) {
            ret = Boolean::New(isolate, true);
        } else {
            ret = Boolean::New(isolate, false);
        }

        info.GetReturnValue().Set(ret);
    }

    class NodeUSBWatcher : public usb_driver::USBWatcher {

        public:
            NodeUSBWatcher(Isolate *isolate, Local<Object> obj)
                : m_isolate(isolate)
            {
                m_jsWatcher.Reset(isolate, obj);
            }

            virtual ~NodeUSBWatcher() {}

            /**
             * Called on device attachment.
             */
            virtual void
            attached(usb_driver::USBDrive *usb_info) {
                emit("attach", usb_info);
            }

            /**
             * Called on device detachment
             */
            virtual void
            detached(usb_driver::USBDrive *usb_info) {
                emit("detach", usb_info);
            }

            /**
             * Called when a device is mounted.
             */
            virtual void
            mount(usb_driver::USBDrive *usb_info) {
                emit("mount", usb_info);
            }

            /**
             * Called when a device is unmounted.
             */
            virtual void
            unmount(usb_driver::USBDrive *usb_info) {
                emit("unmount", usb_info);
            }

        private:
            Persistent<Object> m_jsWatcher;
            Isolate *m_isolate;

            /**
             * Emit an event to
             */
            void
            emit(const char *name, usb_driver::USBDrive *usb_info) {
                assert(usb_info != NULL);

                Local<Object> rcv = Local<Object>::New(m_isolate, m_jsWatcher);
                Handle<Value> argv[1] = { USBDrive_to_Object(m_isolate, usb_info) };

                node::MakeCallback(m_isolate, rcv, String::NewFromUtf8(m_isolate, name), 1, argv);
            }
    };


    void
    RegisterWatcher(const FunctionCallbackInfo<Value> &info)
    {
        Isolate *isolate = info.GetIsolate();

        if(info.Length() < 1)
            THROW_AND_RETURN(isolate, "Wrong number of arguments");

        if(!info[0]->IsObject())
            THROW_AND_RETURN(isolate, "Expected the first argument to be of type object");

        Local<Object> js_watcher = info[0]->ToObject();

        NodeUSBWatcher *watcher = new NodeUSBWatcher(isolate, js_watcher);

        usb_driver::RegisterWatcher(watcher);

        // Return nothing
        info.GetReturnValue().SetNull();
    }

    void
    WaitForEvents(const FunctionCallbackInfo<Value> &info)
    {
        usb_driver::WaitForEvents();
        // Return nothing
        info.GetReturnValue().SetNull();
    }

    void
    GetDevice(const FunctionCallbackInfo<Value> &info)
    {
        auto isolate = info.GetIsolate();

        if(info.Length() < 1)
            THROW_AND_RETURN(isolate, "Wrong number of arguments");

        if(!info[0]->IsString())
            THROW_AND_RETURN(isolate, "Expected the first argument to be of type string");

        String::Utf8Value str(info[0]->ToString());

        auto usb_drive = usb_driver::GetDevice(*str);

        if(usb_drive == NULL) {
            info.GetReturnValue().SetNull();
        } else {
            info.GetReturnValue().Set(USBDrive_to_Object(isolate, usb_drive));
        }
    }

    void
    GetDevices(const FunctionCallbackInfo<Value> &info)
    {
        auto isolate = info.GetIsolate();
        auto devices = usb_driver::GetDevices();

        Handle<Array> array = Array::New(isolate, devices.size());

        if(array.IsEmpty())
            THROW_AND_RETURN(isolate, "Array creation failed");

        for(size_t i = 0; i < devices.size(); ++i) {
            auto device_obj = USBDrive_to_Object(isolate, devices[i]);

            array->Set((int)i, device_obj);
        }

        info.GetReturnValue().Set(array);
    }

    void
    Init(Handle<Object> exports)
    {
        NODE_SET_METHOD(exports, "unmount", Unmount);
        NODE_SET_METHOD(exports, "getDevice", GetDevice);
        NODE_SET_METHOD(exports, "getDevices", GetDevices);
        NODE_SET_METHOD(exports, "registerWatcher", RegisterWatcher);
        NODE_SET_METHOD(exports, "waitForEvents", WaitForEvents);
    }
}  // namespace

NODE_MODULE(usb_driver, node_bindings::Init)

